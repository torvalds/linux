#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import sys
import argparse
import tempfile

from libs import init_logger, log_debug, log_error, log_info, pr_get_sid
from libs import Context

import ci

def check_args(args):
    if not os.path.exists(os.path.abspath(args.config)):
        log_error(f"Invalid parameter(config) {args.config}")
        return False

    if not os.path.exists(os.path.abspath(args.src_dir)):
        log_error(f"Invalid parameter(src_dir) {args.src_dir}")
        return False

    return True

def parse_args():
    ap = argparse.ArgumentParser(description="Run CI tests")
    ap.add_argument('-c', '--config', default='./config.json',
                    help='Configuration file to use. default=./config.json')
    ap.add_argument('-s', '--src-dir', required=True,
                    help='Source directory')
    ap.add_argument('-d', '--dry-run', action='store_true', default=False,
                    help='Run it without uploading the result. default=False')

    # Positional parameter
    ap.add_argument("repo",
                    help="Name of Github repository. i.e. bluez/bluez")
    return ap.parse_args()

# Email Message Templates

EMAIL_MESSAGE = '''This is automated email and please do not reply to this email!

Dear submitter,

Thank you for submitting the patches to the Linux RISC-V mailing list.
This is a CI test results with your patch series:
PW Link:{pw_link}

---Test result---
{content}

---
Regards,
Linux RISC-V bot

'''

def github_pr_post_result(ci_data, i, patch, test):
    pr = ci_data.gh.get_pr(ci_data.config['pr_num'], force=True)

    comment = f'Patch {i+1}: "{patch['name']}"\n'
    comment += f"**{test.name}**\n"
    comment += f"Desc: {test.desc}\n"
    comment += f"Duration: {test.elapsed():.2f} seconds\n"
    comment += f"**Result: {test.verdict.name}**\n"

    if test.output:
        comment += f"Output:\n```\n{test.output}\n```"

    return ci_data.gh.pr_post_comment(pr, comment)

def is_maintainers_only(email_config):
    if 'only-maintainers' in email_config and email_config['only-maintainers']:
        return True
    return False

def get_receivers(email_config, submitter):
    log_debug("Get the list of email receivers")

    receivers = []
    if is_maintainers_only(email_config):
        # Send only to the maintainers
        receivers.extend(email_config['maintainers'])
    else:
        # Send to default-to and submitter
        receivers.append(email_config['default-to'])
        receivers.append(submitter)

    return receivers

def send_email(ci_data, content):
    headers = {}
    email_config = ci_data.config['email']

    body = EMAIL_MESSAGE.format(pw_link=ci_data.series['web_url'],
                                content=content)

    headers['In-Reply-To'] = ci_data.patches[0]['msgid']
    headers['References'] = ci_data.patches[0]['msgid']

    if not is_maintainers_only(email_config):
        headers['Reply-To'] = email_config['default-to']

    receivers = get_receivers(email_config, ci_data.series['submitter']['email'])
    ci_data.email.set_receivers(receivers)
    ci_data.email.compose("Re: " + ci_data.series['name'], body, headers)

    if ci_data.config['dry_run']:
        log_info("Dry-Run is set. Skip sending email")
        return

    log_info("Sending Email...")
    ci_data.email.send()

def report_ci(ci_data, test_list):
    """Generate the CI result and send email"""
    results = ""
    summary = "Test Summary:\n"

    line = "{head:<100}{name:<35}{result:<10}{elapsed:.2f} seconds\n"
    fail_msg = '{head}\nTest: {name} - {result}\nDesc: {desc}\nOutput:\n{output}\n'

    for i in range(len(test_list)):
        sha, tests = test_list[i]
        patch = ci_data.patches[i] # 'name'

        for test in tests:
            if test.verdict == ci.Verdict.PASS:
                # No need to add result of passed tests to simplify the email
                summary += line.format(head=f'Patch {i+1}: "{patch['name']}"',
                                       name=test.name, result='PASS',
                                       elapsed=test.elapsed())
                continue

            # Rest of the verdicts use same output format
            results += "##############################\n"
            results += fail_msg.format(head=f'Patch {i+1}: "{patch['name']}"',
                                       name=test.name, result=test.verdict.name,
                                       desc=test.desc, output=test.output)
            summary += line.format(head=f'Patch {i+1}: "{patch['name']}"',
                                   name=test.name, result=test.verdict.name,
                                   elapsed=test.elapsed())

    if results != "":
        results = "Details\n" + results

    send_email(ci_data, summary + '\n' + results)

def create_test_list(ci_data):
    test_list = []
    # XXX ci_config = ci_data.config['space_details']['kernel']['ci']

    ########################################
    # Test List
    ########################################

    i = 0
    for sha in ci_data.shas:
        tests = []

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "build-rv32-defconfig",
                                  "Builds riscv32 defconfig",
                                  "build_rv32_defconfig.sh"))

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "build-rv64-clang-allmodconfig",
                                  "Builds riscv64 allmodconfig with Clang, and checks for errors and added warnings",
                                  "build_rv64_clang_allmodconfig.sh"))

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "build-rv64-gcc-allmodconfig",
                                  "Builds riscv64 allmodconfig with GCC, and checks for errors and added warnings",
                                  "build_rv64_gcc_allmodconfig.sh"))

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "build-rv64-nommu-k210-defconfig",
                                  "Builds riscv64 defconfig with NOMMU for K210",
                                  "build_rv64_nommu_k210_defconfig.sh"))

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "build-rv64-nommu-k210-virt",
                                  "Builds riscv64 defconfig with NOMMU for the virt platform",
                                  "build_rv64_nommu_virt_defconfig.sh"))

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "checkpatch",
                                  "Runs checkpatch.pl on the patch",
                                  "checkpatch.sh"))

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "dtb-warn-rv64",
                                  "Checks for Device Tree warnings/errors",
                                  "dtb_warn_rv64.sh"))

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "header-inline",
                                  "Detects static functions without inline keyword in header files",
                                  "header_inline.sh"))

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "kdoc",
                                  "Detects for kdoc errors",
                                  "kdoc.sh"))

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "module-param",
                                  "Detect module_param changes",
                                  "module_param.sh"))

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "verify-fixes",
                                  "Verifies that the Fixes: tags exist",
                                  "verify_fixes.sh"))

        tests.append(ci.ShellTest(ci_data, ci_data.patches[i],
                                  "verify-signedoff",
                                  "Verifies that Signed-off-by: tags are correct",
                                  "verify_signedoff.sh"))
        t = (sha, tests)
        test_list.append(t)
        i += 1

    return test_list

def run_ci(ci_data):
    num_fails = 0

    test_list = create_test_list(ci_data)

    log_info(f"Test list is created: {len(test_list)}")
    log_debug("+--------------------------+")
    log_debug("|          Run CI          |")
    log_debug("+--------------------------+")

    for i in range(len(test_list)):
        sha, tests = test_list[i]
        patch = ci_data.patches[i] # 'name'
        with tempfile.TemporaryDirectory(dir="/build") as worktree:
            cmd = ['worktree', 'add', worktree, sha]
            if ci_data.src_repo.git(cmd):
                log_error(f"Failed to create worktree")
                continue

            for test in tests:
                log_info("##############################")
                log_info(f'## CI: Patch {i+1}: "{patch['name']}"')
                log_info(f"## CI: {test.name}")
                log_info("##############################")

                try:
                    test.run(worktree=worktree)
                except ci.EndTest as e:
                    log_error(f"Test Ended(Failure): {test.name}:{test.verdict.name}")
                except Exception as e:
                    log_error(f"Test Ended(Exception): {test.name}: {e.__class__}")
                finally:
                    test.post_run()

                if test.verdict != ci.Verdict.PASS:
                    num_fails += 1

                if ci_data.config['dry_run']:
                    log_info("Skip submitting result to Github: dry_run=True")
                    continue

                log_debug("Submit the result to github")
                if not github_pr_post_result(ci_data, i, patch, test):
                    log_error("Failed to submit the result to Github")

    log_info(f"Total number of failed test: {num_fails}")
    log_debug("+--------------------------+")
    log_debug("|        ReportCI          |")
    log_debug("+--------------------------+")
    report_ci(ci_data, test_list)

    return num_fails

def main():
    global config, pw, gh, src_repo, email

    init_logger("PW_CI", verbose=True)

    if 'GITHUB_REF' not in os.environ:
        log_error("GITHUB_REF environment not set")
        sys.exit(1)

    pr_num = int(os.environ['GITHUB_REF'].removeprefix("refs/pull/").removesuffix("/merge"))

    args = parse_args()
    if not check_args(args):
        sys.exit(1)

    ci_data = Context(config_file=os.path.abspath(args.config),
                      github_repo=args.repo,
                      src_dir=args.src_dir,
                      dry_run=args.dry_run,
                      pr_num=pr_num,
                      space='kernel')

    pr = ci_data.gh.get_pr(pr_num, force=True)
    sid = pr_get_sid(pr.title)

    # If PR is not created for Patchwork (no key string), ignore this PR and
    # stop running the CI
    if not sid:
        log_error("Not a valid PR. No need to run")
        sys.exit(1)

    cmd = ['log', '-1', '--pretty=%H', '.github/scripts/sync_patchwork.py']
    if ci_data.src_repo.git(cmd):
        log_error("Failed to get base commit")
        sys.exit(1)

    base_sha = ci_data.src_repo.stdout.strip()

    if len(base_sha) == 0:
        log_error("Failed to get base commit")
        sys.exit(1)

    cmd = ['rev-list', '--reverse', f'{base_sha}..HEAD']
    if ci_data.src_repo.git(cmd):
        log_error("Failed to list of commits")
        sys.exit(1)

    shas = ci_data.src_repo.stdout.split()
    if len(shas) == 0:
        log_error("Failed to get list of commits")
        sys.exit(1)

    ci_data.update_series(ci_data.pw.get_series(sid), shas)

    if len(ci_data.shas) != len(ci_data.patches):
        log_error("Git and patchwork mismatch")
        sys.exit(1)

    num_fails = run_ci(ci_data)

    log_debug("----- DONE -----")

    sys.exit(num_fails)

if __name__ == "__main__":
    main()
