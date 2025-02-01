#!/usr/bin/env python3

import unittest
from typing import Iterable, List

from ..veristat_compare import parse_table, VeristatFields


def gen_csv_table(records: Iterable[str]) -> List[str]:
    return [
        ",".join(VeristatFields.headers()),
        *records,
    ]


class TestVeristatCompare(unittest.TestCase):
    def test_parse_table_ignore_new_prog(self):
        table = gen_csv_table(
            [
                "prog_file.bpf.o,prog_name,N/A,success,N/A,N/A,1,N/A",
            ]
        )
        veristat_info = parse_table(table)
        self.assertEqual(veristat_info.table, [])
        self.assertFalse(veristat_info.changes)
        self.assertFalse(veristat_info.new_failures)

    def test_parse_table_ignore_removed_prog(self):
        table = gen_csv_table(
            [
                "prog_file.bpf.o,prog_name,success,N/A,N/A,1,N/A,N/A",
            ]
        )
        veristat_info = parse_table(table)
        self.assertEqual(veristat_info.table, [])
        self.assertFalse(veristat_info.changes)
        self.assertFalse(veristat_info.new_failures)

    def test_parse_table_new_failure(self):
        table = gen_csv_table(
            [
                "prog_file.bpf.o,prog_name,success,failure,MISMATCH,1,1,+0 (+0.00%)",
            ]
        )
        veristat_info = parse_table(table)
        self.assertEqual(
            veristat_info.table,
            [["prog_file.bpf.o", "prog_name", "success -> failure (!!)", "+0.00 %"]],
        )
        self.assertTrue(veristat_info.changes)
        self.assertTrue(veristat_info.new_failures)

    def test_parse_table_new_changes(self):
        table = gen_csv_table(
            [
                "prog_file.bpf.o,prog_name,failure,success,MISMATCH,0,0,+0 (+0.00%)",
                "prog_file.bpf.o,prog_name_increase,failure,failure,MATCH,1,2,+1 (+100.00%)",
                "prog_file.bpf.o,prog_name_decrease,success,success,MATCH,1,1,-1 (-100.00%)",
            ]
        )
        veristat_info = parse_table(table)
        self.assertEqual(
            veristat_info.table,
            [
                ["prog_file.bpf.o", "prog_name", "failure -> success", "+0.00 %"],
                ["prog_file.bpf.o", "prog_name_increase", "failure", "+100.00 %"],
                ["prog_file.bpf.o", "prog_name_decrease", "success", "-100.00 %"],
            ],
        )
        self.assertTrue(veristat_info.changes)
        self.assertFalse(veristat_info.new_failures)


if __name__ == "__main__":
    unittest.main()
