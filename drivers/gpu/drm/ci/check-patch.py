#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# check-patch.py: run checkpatch.pl across all commits in a branch
#
# Based on qemu/.gitlab-ci.d/check-patch.py
#
# Copyright (C) 2020 Red Hat, Inc.
# Copyright (C) 2022 Collabora Ltd.

import os
import os.path
import sys
import subprocess

repourl = "https://gitlab.freedesktop.org/%s.git" % os.environ["CI_MERGE_REQUEST_PROJECT_PATH"]

# GitLab CI environment does not give us any direct info about the
# base for the user's branch. We thus need to figure out a common
# ancestor between the user's branch and current git master.
os.environ["GIT_DEPTH"] = "1000"
subprocess.call(["git", "remote", "remove", "check-patch"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.check_call(["git", "remote", "add", "check-patch", repourl])
subprocess.check_call(["git", "fetch", "check-patch", os.environ["CI_MERGE_REQUEST_TARGET_BRANCH_NAME"]],
                      stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL)

ancestor = subprocess.check_output(["git", "merge-base",
                                    "check-patch/%s" % os.environ["CI_MERGE_REQUEST_TARGET_BRANCH_NAME"], "HEAD"],
                                   universal_newlines=True)

ancestor = ancestor.strip()

log = subprocess.check_output(["git", "log", "--format=%H %s",
                               ancestor + "..."],
                              universal_newlines=True)

subprocess.check_call(["git", "remote", "rm", "check-patch"])

if log == "":
    print("\nNo commits since %s, skipping checks\n" % ancestor)
    sys.exit(0)

errors = False

print("\nChecking all commits since %s...\n" % ancestor, flush=True)

ret = subprocess.run(["scripts/checkpatch.pl",
                      "--terse",
                      "--types", os.environ["CHECKPATCH_TYPES"],
                      "--git", ancestor + "..."])

if ret.returncode != 0:
    print("    ❌ FAIL one or more commits failed scripts/checkpatch.pl")
    sys.exit(1)

sys.exit(0)
