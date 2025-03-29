#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import sys
import logging
import argparse

from datetime import datetime
from github import Github

from libs import init_logger, log_debug, log_error, log_info, pr_get_sid
from libs import GithubTool

dry_run = False

MAGIC_LINE = "BlueZ Testbot Message:"
MAGIC_LINE_2 = "BlueZ Testbot Message #2:"
MAGIC_LINE_3 = "BlueZ Testbot Message #3:"
MAGIC_LINE_4 = "BlueZ Testbot Message #4:"

PATCH_SUBMISSION_MSG = '''
This is an automated message and please do not change or delete.

Dear submitter,

Thanks for submitting the pull request to the BlueZ github repo.
Currently, the BlueZ repo in Github is only for CI and testing purposes,
and not accepting any pull request at this moment.

If you still want us to review your patch and merge them, please send your
patch to the Linux Bluetooth mailing list(linux-bluetooth@vger.kernel.org).

For more detail about submitting a patch to the mailing list,
Please refer \"Submitting patches\" section in the HACKING file in the source.

Note that this pull request will be closed in the near future.

Best regards,
BlueZ Team
'''

PATCH_SUBMISSION_MSG_2 = '''
This is an automated message and please do not change or delete.

Dear submitter,

This is a friendly reminder that this pull request will be closed within
a week or two.

If you already submitted the patches to the Linux Bluetooth mailing list
(linux-bluetooth@vger.kernel.org) for review, Please close this pull
request.

If you haven't submitted the patches but still want us to review your patch,
please send your patch to the Linux Bluetooth mailing list
(linux-bluetooth@vger.kernel.org).

For more detail about submitting a patch to the mailing list,
Please refer \"Submitting patches\" section in the HACKING file in the source.

Note that this pull request will be closed in a week or two.

Best regards,
BlueZ Team
'''

PATCH_SUBMISSION_MSG_3 = '''
This is an automated message and please do not change or delete.

Dear submitter,

Thanks for submitting the pull request to the BlueZ github repo.
Currently, the BlueZ repo in Github is only for CI and testing purposes,
and not accepting any pull request at this moment.

If you still want us to review your patch and merge them, please send your
patch to the Linux Bluetooth mailing list(linux-bluetooth@vger.kernel.org).

For more detail about submitting a patch to the mailing list,
Please refer \"Submitting patches\" section in the HACKING file in the source.

Note that this pull request will be closed in the near future.

Best regards,
BlueZ Team
'''

PATCH_SUBMISSION_MSG_4 = '''
This is an automated message and please do not change or delete.

Closing without taking any action.

Best regards,
BlueZ Team
'''

def get_comment_str(magic_line):
    """
    Generate the comment string including magic_line
    """
    if magic_line == MAGIC_LINE:
        msg = PATCH_SUBMISSION_MSG
    if magic_line == MAGIC_LINE_2:
        msg = PATCH_SUBMISSION_MSG_2
    if magic_line == MAGIC_LINE_3:
        msg = PATCH_SUBMISSION_MSG_3
    if magic_line == MAGIC_LINE_4:
        msg = PATCH_SUBMISSION_MSG_4

    return magic_line + "\n\n" + msg

def get_magic_line(body):
    if (body.find(MAGIC_LINE) >= 0):
        return MAGIC_LINE
    if (body.find(MAGIC_LINE_2) >= 0):
        return MAGIC_LINE_2
    if (body.find(MAGIC_LINE_3) >= 0):
        return MAGIC_LINE_3
    if (body.find(MAGIC_LINE_4) >= 0):
        return MAGIC_LINE_4
    return None

def pr_add_comment(gh, pr, magic_line):
    """
    Add the comment based on magic line
    """
    comment = get_comment_str(magic_line)

    log_debug(f"Add PR comments{magic_line}:\n{comment}")

    if dry_run:
        log_info("Dry-Run: Skip adding comment to PR")
        return

    gh.pr_post_comment(pr, comment)

def pr_close(gh, pr):
    """
    Close pull request
    """
    log_debug(f"Close PR{pr.number}")

    if dry_run:
        log_info("Dry-Run: Skip closing PR")
        return

    gh.pr_close(pr)

def get_latest_comment(gh, pr):
    """
    Search through the comments and find the latest comment
    """
    comments = gh.pr_get_issue_comments(pr)
    if not comments:
        log_error("Unable to get the comments")
        return None

    log_info(f"PR#{pr.number} Comment count: {comments.totalCount}")

    for comment in comments.reversed:
        magic_line = get_magic_line(comment.body)
        if magic_line != None:
            log_debug(f"The most recent comment: {magic_line}")
            return magic_line

    log_debug("No bluez comment found")
    return None

def update_pull_request(gh, pr, days_created):

    if days_created > 14:
        log_debug("Days created > 14")
        log_debug("PR is more than 2 weeks and close the PR")
        pr_close(gh, pr)

def manage_pr(gh):

    prs = gh.get_prs(force=True)
    log_info(f"Pull Request count: {prs.totalCount}")

    # Handle each PR
    for pr in prs:
        log_debug(f"Check PR#_{pr.number}")

        # Check if this PR is created with Patchwork series.
        # If yes, stop processing.
        pw_sid = pr_get_sid(pr.title)
        if pw_sid:
            log_info(f"PR is created with Patchwork SID: {pw_sid}")
            continue

        # Calcuate the number of days since PR was created
        delta = datetime.now().astimezone(pr.created_at.tzinfo) - pr.created_at
        days_created = delta.days

        log_debug(f"PR opended {days_created} days ago")

        # Update the PR
        update_pull_request(gh, pr, days_created)

def parse_args():
    """ Parse input argument """

    ap = argparse.ArgumentParser(description="Clean up PR")
    ap.add_argument('-d', '--dry-run', action='store_true', default=False,
                    help='Run it without updating the PR')
    # Positional paramter
    ap.add_argument("repo",
                    help="Name of Github repository. i.e. bluez/bluez")
    return ap.parse_args()

def main():

    global dry_run

    init_logger("ManagePR", verbose=True)

    args = parse_args()

    # Make sure GITHUB_TOKEN exists
    if 'GITHUB_TOKEN' not in os.environ:
        log_error("Set GITHUB_TOKEN environment variable")
        sys.exit(1)

    # Initialize github repo object
    try:
        gh = GithubTool(args.repo, os.environ['GITHUB_TOKEN'])
    except:
        log_error("Failed to initialize GithubTool class")
        sys.exit(1)

    dry_run = args.dry_run

    manage_pr(gh)

if __name__ == "__main__":
    main()

