#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2024 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

#
# Pulls build/run results from the kselftest kernel logs, and
# publishes them to SQUAD.
#

import argparse
import json
import os
import os.path
import pathlib
import pprint
import re
import requests
import sys
import tempfile

from pathlib import Path
from tap import parser

SQUAD_TOKEN = os.getenv('SQUAD_TOKEN')
SQUAD_URL = "https://mazarinen.tail1c623.ts.net/api/submit"
SQUAD_GROUP = "riscv-linux"
SQUAD_PROJECT = "linux-all"
SQUAD_CI_ENV = "qemu"

def parse_bpf_kselftest(kselftest_file, results):
    bpf_start_pat = re.compile(r'^#\s+selftests:\s+([a-z_\-/]+):\s+([a-z_\-/]+)')
    bpf_end_pat = re.compile(r'^(not ok|ok)\s+([0-9]+)\s+selftests:\s+([a-z_\-/]+):\s+([a-z_\-/]+)')
    bpf_test_pat = re.compile(r'^#\s+#([0-9]+)\s+([a-z_\-/]+):(OK|FAIL)')

    curr_start = None
    curr_end = None
    for i in kselftest_file:
        if (m := bpf_start_pat.match(i)):
            group = m.group(1)
            test = m.group(2)
            if curr_start and not curr_end:
                print(f"PREMATURE START: group: {group} test: {test}", file=sys.stderr)
            curr_start = m
            curr_end = None
            continue

        if (m := bpf_end_pat.match(i)):
            group = "kselftest-" + m.group(3)
            test = group + "/" + m.group(4) + "__allsubtests"
            res = False if m.group(1) == "not ok" else True
            if not curr_start or curr_start.group(1) != m.group(3):
                print(f"PREMATURE END: group end: {group} test: {test}", file=sys.stderr)
                res = False
            curr_end = m

            if group not in results:
                results[group] = {}
            if "tests" not in results[group]:
                results[group]["tests"] = {}
            if test not in results[group]["tests"]:
                results[group]["tests"][test] = {}

            results[group]["tests"][test]["result"] = "pass" if res else "fail"
            results[group]["log"] = kselftest_file.name
            continue

        if (m := bpf_test_pat.match(i)):
            sub_test_id = m.group(1)
            sub_test_name = m.group(2)
            sub_res = False if m.group(3) == "FAIL" else True
            if not curr_start:
                print(f"PREMATURE TEST: test: {sub_test_name}", file=sys.stderr)
                continue

            group = "kselftest-" + curr_start.group(1)
            test = group + "/" + curr_start.group(2) + "__" + sub_test_name

            if "tests" not in results[group]:
                results[group]["tests"] = {}
            if test not in results[group]["tests"]:
                results[group]["tests"][test] = {}

            results[group]["tests"][test]["result"] = "pass" if sub_res else "fail"
            results[group]["log"] = kselftest_file.name

    if curr_start and not curr_end:
        group = "kselftest-" + curr_start.group(1)
        test =  group + "/" + curr_start.group(2) + "__allsubtests"
        print(f"END MISSING: group end: {group} test: {test}", file=sys.stderr)
        if test not in results[group]["tests"]:
            results[group]["tests"][test] = {}
        results[group]["tests"][test]["result"] = "fail"

    return results

def parse_kselftest(kselftest_file, results):
    description_pat = re.compile(r"selftests:\s+(\S+)\s+(\S+)")

    p = parser.Parser()
    for l in p.parse(kselftest_file):
        if l.category == "test":
            description = description_pat.match(l.description)
            if not description:
                print(f"BAD SELFTEST STRING: {l.description}", file=sys.stderr)
                continue

            group = "kselftest-" + description.group(1)[:-1]
            test =  group + "/" + description.group(2)

            if group not in results:
                results[group] = {}
            if "tests" not in results[group]:
                results[group]["tests"] = {}
            if test not in results[group]["tests"]:
                results[group]["tests"][test] = {}

            # XXX Use l.directive.skip?

            results[group]["tests"][test]["result"] = "pass" if l.ok else "fail"
            results[group]["log"] = kselftest_file.name

    return results

#
# The toplevel file needs special handling, when submitting the
# results to SQUAD. The SQUAD REST API only allows for *one* full log
# per POST, but the toplevel log is a collection of builds/tests. This
# means that each test in the toplevel, that has a log needs a POST of
# its own.
#
def parse_toplevel(top_file, results):
    build_fail_pat = re.compile(r"^::error::FAIL Build (kernel|selftest) (\S+) \"(\S+)\"")
    build_ok_pat = re.compile(r"^::notice::OK Build (kernel|selftest) (\S+)")
    test_fail_pat = re.compile(r"^::error::FAIL Test kernel (\S+) (\S+) (\S+) (\S+) (\S+) (\S+) \S+ \"(\S+)\"")
    test_ok_pat = re.compile(r"^::notice::OK Test kernel (\S+) (\S+) (\S+) (\S+) (\S+) (\S+)")
    build_name_pat = re.compile(r"^build_name (\S+)")

    for i in top_file:
        group = None
        test = None
        log = None
        res = False

        if (m := build_name_pat.match(i)):
            results["build_name"] = m.group(1)
            continue

        if (m := build_fail_pat.match(i)):
            group = m.group(2)
            test = group + "/" + "build_" + m.group(1)
            log = m.group(3)
            res = False

        if (m := build_ok_pat.match(i)):
            group = m.group(2)
            test = group + "/" + "build_" + m.group(1)
            res = True

        if (m := test_fail_pat.match(i)):
            group = m.group(1)
            test = group + "/" + m.group(2) + "__" + m.group(3) + "__"  + m.group(4)\
                + "__" + m.group(5) + "__" + m.group(6)
            log = m.group(7)
            res = False

        if (m := test_ok_pat.match(i)):
            group = m.group(1)
            test = group + "/" + m.group(2) + "__" + m.group(3) + "__"  + m.group(4)\
                + "__" + m.group(5) + "__" + m.group(6)
            res = True

        if group and test:
            if group not in results:
                results[group] = {}

            results[group]["log_per_test"] = True

            if "tests" not in results[group]:
                results[group]["tests"] = {}
            if "tests-log" not in results[group]:
                results[group]["tests-log"] = {}
            if test not in results[group]["tests"]:
                results[group]["tests"][test] = {}

            results[group]["tests"][test]["result"] = "pass" if res else "fail"
            if log:
                if "tests-log" not in results[group]:
                    results[group]["tests-log"] = {}

                l = os.path.dirname(top_file.name) + "/" + log
                results[group]["tests-log"][test] = l

    return results

def parse_args():
    parser = argparse.ArgumentParser(description = 'Output Squad json from kselftest runs')

    parser.add_argument("--fake-curl", action="store_true", help = 'Dry run showing curl equivalent')
    parser.add_argument("--branch", default="please_set_me", help = 'Git branch ref')
    parser.add_argument("--job-url", default="http://example.com/notset", help = 'Job URL')
    parser.add_argument("--selftest-bpf-log", help = 'BPF kselftest log file')
    parser.add_argument("--selftest-log-dir", default=None, help = 'Kselftest log files directory')
    parser.add_argument("--toplevel-log", required=True, help = 'Toplevel "kselftest.log" file')

    return parser.parse_args()

def submit_fake_curl(testsuite, tests_dict, log, job_url, branch, build_name, o_path):
    metadata_json = o_path / (testsuite.replace("/", "_") + "--metadata.json")
    jstr = json.dumps({"job_url" : job_url,
                       "branch" : branch
                       }, indent=4)
    metadata_json.write_text(jstr)
    print(f"metadata: {jstr}")

    tests_json = o_path / (testsuite.replace("/", "_") + "--tests.json")
    jstr = json.dumps(tests_dict, indent=4)
    tests_json.write_text(jstr)
    print(f"tests: {jstr}")

    print(f'curl --header "Authorization: token {SQUAD_TOKEN}" \\')
    print(f'  --form tests=@{str(o_path.absolute()) + "/" + tests_json.name} \\')
    print(f'  --form metadata=@{str(o_path.absolute()) + "/" + metadata_json.name} \\')
    if log:
        print(f'  --form log=@{log} \\')
    print(f'  {SQUAD_URL}/{SQUAD_GROUP}/{SQUAD_PROJECT}/{build_name}/{SQUAD_CI_ENV}')

def submit_squad(testsuite, tests_dict, log, job_url, branch, build_name, o_path):
    full_url = f"{SQUAD_URL}/{SQUAD_GROUP}/{SQUAD_PROJECT}/{build_name}/{SQUAD_CI_ENV}"

    metadata_json = o_path / (testsuite.replace("/", "_") + "--metadata.json")
    jstr = json.dumps({"job_url" : job_url,
                       "branch" : branch
                       }, indent=4)
    metadata_json.write_text(jstr)
    # print(f"metadata: {jstr}")

    tests_json = o_path / (testsuite.replace("/", "_") + "--tests.json")
    jstr = json.dumps(tests_dict, indent=4)
    tests_json.write_text(jstr)
    # print(f"tests: {jstr}")

    headers = {
        "Authorization": f"token {SQUAD_TOKEN}"
    }

    files = {
        "tests": tests_json.open(mode='rb'),
        "metadata": metadata_json.open(mode='rb')
    }

    if log:
        files["log"] = open(f"{log}", 'rb')

    response = requests.post(full_url, headers=headers, files=files)
    print(f"Request completed with status code: {response.status_code} text: {response.text}")

if __name__ == "__main__":
    args = parse_args()

    results = {}

    t = os.path.expanduser(args.toplevel_log)
    with open(t, 'r') as top:
        results = parse_toplevel(top, results)

    if args.selftest_log_dir:
        for f in Path(args.selftest_log_dir).glob("test_kernel__*.log"):
            a = os.path.expanduser(f)
            with open(a, 'r') as all:
                results = parse_kselftest(all, results)

    if args.selftest_bpf_log:
        b = os.path.expanduser(args.selftest_bpf_log)
        with open(b, 'r') as bpf:
            results = parse_bpf_kselftest(bpf, results)

    submit = submit_fake_curl if args.fake_curl else submit_squad

    with tempfile.TemporaryDirectory() as temp_dir:
        o_path = Path(temp_dir)

        for testsuite in results:
            if testsuite == "build_name":
                continue

            if results[testsuite].get("log_per_test", False):
                for test in results[testsuite]["tests"]:
                    log = results[testsuite]["tests-log"].get(test, None)
                    submit(testsuite, {test : results[testsuite]["tests"][test] },\
                           log, args.job_url, args.branch, results["build_name"], o_path)
            else:
                log = results[testsuite].get("log", None)
                submit(testsuite, results[testsuite]["tests"], log, args.job_url,\
                       args.branch, results["build_name"], o_path)
