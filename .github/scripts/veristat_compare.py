#!/usr/bin/env python3

# This script reads a CSV file produced by the following invocation:
#
#   veristat --emit file,prog,verdict,states \
#            --output-format csv \
#            --compare ...
#
# And produces a markdown summary for the file.
# The summary is printed to standard output and appended to a file
# pointed to by GITHUB_STEP_SUMMARY variable.
#
# Script exits with return code 1 if there are new failures in the
# veristat results.
#
# For testing purposes invoke as follows:
#
#  GITHUB_STEP_SUMMARY=/dev/null python3 veristat-compare.py test.csv
#
# File format (columns):
#  0. file_name
#  1. prog_name
#  2. verdict_base
#  3. verdict_comp
#  4. verdict_diff
#  5. total_states_base
#  6. total_states_comp
#  7. total_states_diff
#
# Records sample:
#  file-a,a,success,failure,MISMATCH,12,12,+0 (+0.00%)
#  file-b,b,success,success,MATCH,67,67,+0 (+0.00%)
#
# For better readability suffixes '_OLD' and '_NEW'
# are used instead of '_base' and '_comp' for variable
# names etc.

import io
import os
import sys
import re
import csv
import logging
import argparse
import enum
from dataclasses import dataclass
from typing import Dict, Iterable, List, Final


TRESHOLD_PCT: Final[int] = 0

SUMMARY_HEADERS = ["File", "Program", "Verdict", "States Diff (%)"]

# expected format: +0 (+0.00%) / -0 (-0.00%)
TOTAL_STATES_DIFF_REGEX = (
    r"(?P<absolute_diff>[+-]\d+) \((?P<percentage_diff>[+-]\d+\.\d+)\%\)"
)


TEXT_SUMMARY_TEMPLATE: Final[str] = (
    """
# {title}

{table}
""".strip()
)

HTML_SUMMARY_TEMPLATE: Final[str] = (
    """
# {title}

<details>
<summary>Click to expand</summary>

{table}
</details>
""".strip()
)

GITHUB_MARKUP_REPLACEMENTS: Final[Dict[str, str]] = {
    "->": "&rarr;",
    "(!!)": ":bangbang:",
}

NEW_FAILURE_SUFFIX: Final[str] = "(!!)"


class VeristatFields(str, enum.Enum):
    FILE_NAME = "file_name"
    PROG_NAME = "prog_name"
    VERDICT_OLD = "verdict_base"
    VERDICT_NEW = "verdict_comp"
    VERDICT_DIFF = "verdict_diff"
    TOTAL_STATES_OLD = "total_states_base"
    TOTAL_STATES_NEW = "total_states_comp"
    TOTAL_STATES_DIFF = "total_states_diff"

    @classmethod
    def headers(cls) -> List[str]:
        return [
            cls.FILE_NAME,
            cls.PROG_NAME,
            cls.VERDICT_OLD,
            cls.VERDICT_NEW,
            cls.VERDICT_DIFF,
            cls.TOTAL_STATES_OLD,
            cls.TOTAL_STATES_NEW,
            cls.TOTAL_STATES_DIFF,
        ]


@dataclass
class VeristatInfo:
    table: list
    changes: bool
    new_failures: bool

    def get_results_title(self) -> str:
        if self.new_failures:
            return "There are new veristat failures"

        if self.changes:
            return "There are changes in verification performance"

        return "No changes in verification performance"

    def get_results_summary(self, markup: bool = False) -> str:
        title = self.get_results_title()
        if not self.table:
            return f"# {title}\n"

        template = TEXT_SUMMARY_TEMPLATE
        table = format_table(headers=SUMMARY_HEADERS, rows=self.table)

        if markup:
            template = HTML_SUMMARY_TEMPLATE
            table = github_markup_decorate(table)

        return template.format(title=title, table=table)


def get_state_diff(value: str) -> float:
    if value == "N/A":
        return 0.0

    matches = re.match(TOTAL_STATES_DIFF_REGEX, value)
    if not matches:
        raise ValueError(f"Failed to parse total states diff field value '{value}'")

    if percentage_diff := matches.group("percentage_diff"):
        return float(percentage_diff)

    raise ValueError(f"Invalid {VeristatFields.TOTAL_STATES_DIFF} field value: {value}")


def parse_table(csv_file: Iterable[str]) -> VeristatInfo:
    reader = csv.DictReader(csv_file)
    assert reader.fieldnames == VeristatFields.headers()

    new_failures = False
    changes = False
    table = []

    for record in reader:
        add = False

        verdict_old, verdict_new = (
            record[VeristatFields.VERDICT_OLD],
            record[VeristatFields.VERDICT_NEW],
        )

        # Ignore results from completely new and removed programs
        if "N/A" in [verdict_new, verdict_old]:
            continue

        if record[VeristatFields.VERDICT_DIFF] == "MISMATCH":
            changes = True
            add = True
            verdict = f"{verdict_old} -> {verdict_new}"
            if verdict_new == "failure":
                new_failures = True
                verdict += f" {NEW_FAILURE_SUFFIX}"
        else:
            verdict = record[VeristatFields.VERDICT_NEW]

        diff = get_state_diff(record[VeristatFields.TOTAL_STATES_DIFF])
        if abs(diff) > TRESHOLD_PCT:
            changes = True
            add = True

        if not add:
            continue

        table.append(
            [
                record[VeristatFields.FILE_NAME],
                record[VeristatFields.PROG_NAME],
                verdict,
                f"{diff:+.2f} %",
            ]
        )

    return VeristatInfo(table=table, changes=changes, new_failures=new_failures)


def github_markup_decorate(input_str: str) -> str:
    for text, markup in GITHUB_MARKUP_REPLACEMENTS.items():
        input_str = input_str.replace(text, markup)
    return input_str


def format_table(headers: List[str], rows: List[List[str]]) -> str:
    column_width = [
        max(len(row[column_idx]) for row in [headers] + rows)
        for column_idx in range(len(headers))
    ]

    # Row template string in the following format:
    # "{0:8}|{1:10}|{2:15}|{3:7}|{4:10}"
    row_template = "|".join(
        f"{{{idx}:{width}}}" for idx, width in enumerate(column_width)
    )
    row_template_nl = f"|{row_template}|\n"

    with io.StringIO() as out:
        out.write(row_template_nl.format(*headers))

        separator_row = ["-" * width for width in column_width]
        out.write(row_template_nl.format(*separator_row))

        for row in rows:
            row_str = row_template_nl.format(*row)
            out.write(row_str)

        return out.getvalue()


def main(compare_csv_filename: os.PathLike, output_filename: os.PathLike) -> None:
    with open(compare_csv_filename, newline="", encoding="utf-8") as csv_file:
        veristat_results = parse_table(csv_file)

    sys.stdout.write(veristat_results.get_results_summary())

    with open(output_filename, encoding="utf-8", mode="a") as file:
        file.write(veristat_results.get_results_summary(markup=True))

    if veristat_results.new_failures:
        return 1

    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Print veristat comparison output as markdown step summary"
    )
    parser.add_argument("filename")
    args = parser.parse_args()
    summary_filename = os.getenv("GITHUB_STEP_SUMMARY")
    if not summary_filename:
        logging.error("GITHUB_STEP_SUMMARY environment variable is not set")
        sys.exit(1)
    sys.exit(main(args.filename, summary_filename))
