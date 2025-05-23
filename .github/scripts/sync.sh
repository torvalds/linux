#!/bin/bash

set -euo pipefail

# Assumptions:
#   Run from git source tree, e.g. /build/linux
#
# This script syncs remote "upstream" branches to the local git repo.
# The script will create a $ORIGIN_BRANCH, which is a clone of of the
# upstream $UPSTREAM_BRANCH, and a $WORKFLOW_BRANCH which is the
# upstream with the latest CI applied as on commit on top.
#
# Subsequent jobs will use these branches, e.g. to run Patchwork CI.

echo "Environment Variables:"
echo "   Workflow:   ${GITHUB_WORKFLOW:-notset}"
echo "   Action:     ${GITHUB_ACTION:-notset}"
echo "   Actor:      ${GITHUB_ACTOR:-notset}"
echo "   Repository: ${GITHUB_REPOSITORY:-notset}"
echo "   Event-name: ${GITHUB_EVENT_NAME:-notset}"
echo "   Event-path: ${GITHUB_EVENT_PATH:-notset}"
echo "   Workspace:  ${GITHUB_WORKSPACE:-notset}"
echo "   SHA:        ${GITHUB_SHA:-notset}"
echo "   REF:        ${GITHUB_REF:-notset}"
echo "   HEAD-REF:   ${GITHUB_HEAD_REF:-notset}"
echo "   BASE-REF:   ${GITHUB_BASE_REF:-notset}"
echo "   PWD:        $(pwd)"
echo "   Repo:       ${GITHUB_REPOSITORY:-notset}"

tmpdir=$(mktemp -d -p /build)

cleanup() {
    git remote remove upstream
    echo "bye $tmpdir"
    rm -rf $tmpdir
}

trap cleanup EXIT

# e.g. "git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git"
UPSTREAM_REPO=$1
# e.g. "master"
UPSTREAM_BRANCH=$2
# e.g. "master"
ORIGIN_BRANCH=$3
# e.g. workflow
WORKFLOW_BRANCH=$4
#e.g. https://github.com/linux-riscv/github-ci.git
CI_REPO=$5

echo ">>> Setup repo"
echo "$ git remote set-url origin $GITHUB_REPOSITORY"
git remote set-url origin "https://$GITHUB_ACTOR:$ACTION_TOKEN@github.com/$GITHUB_REPOSITORY"
echo "$ git remote add upstream $UPSTREAM_REPO"
git remote add upstream "$UPSTREAM_REPO"
echo "$ git fetch upstream $UPSTREAM_BRANCH"
git fetch upstream --tags $UPSTREAM_BRANCH

echo ">>> Check Origin and Upstream"
if git rev-parse --verify --quiet origin/$ORIGIN_BRANCH &>/dev/null; then
    ORIGIN_HEAD=$(git log -1 --format=%H origin/$ORIGIN_BRANCH)
else
    ORIGIN_HEAD=noneexisting
fi
echo "ORIGIN_HEAD: $ORIGIN_HEAD"
UPSTREAM_HEAD=$(git log -1 --format=%H upstream/$UPSTREAM_BRANCH)
echo "UPSTREAM_HEAD: $UPSTREAM_HEAD"

if [[ "$ORIGIN_HEAD" != "$UPSTREAM_HEAD" ]]; then
    echo "Repos are NOT synced. Need to merge..."
    echo ">>> Sync origin with upstream"
    echo "$ git remote set-branches origin *"
    git remote set-branches origin '*'
    echo "$ git fetch origin"
    git fetch origin
    echo "$ git push -f origin refs/remotes/upstream/$UPSTREAM_BRANCH:refs/heads/$ORIGIN_BRANCH"
    git push -f origin "refs/remotes/upstream/$UPSTREAM_BRANCH:refs/heads/$ORIGIN_BRANCH"
    echo "$ git push -f origin refs/tags/*"
    git push -f origin "refs/tags/*"
fi

echo ">>> Prepare CI repo for workflow"
echo "$ git clone $CI_REPO $tmpdir/ci"
git clone $CI_REPO $tmpdir/ci

if ! git rev-parse --verify --quiet origin/$WORKFLOW_BRANCH &>/dev/null; then
    echo "$ git checkout -B $ORIGIN_BRANCH origin/$ORIGIN_BRANCH"
    git checkout -B $ORIGIN_BRANCH origin/$ORIGIN_BRANCH
else
    echo "$ git checkout -B $WORKFLOW_BRANCH origin/$WORKFLOW_BRANCH"
    git checkout -B $WORKFLOW_BRANCH origin/$WORKFLOW_BRANCH
fi

update() {
    echo "$ git checkout -B $WORKFLOW_BRANCH origin/$ORIGIN_BRANCH"
    git checkout -B $WORKFLOW_BRANCH origin/$ORIGIN_BRANCH
    echo "$ cp -R $tmpdir/ci/.github ."
    cp -R $tmpdir/ci/.github .
    echo "$ git add --all --force .github"
    git add --all --force .github
    echo "$ git commit --all --message \"Adding CI files\""
    git commit --all --message "Adding CI files"
    echo "$ git branch"
    git branch
    echo "$ git push -f origin $WORKFLOW_BRANCH"
    git push -f origin $WORKFLOW_BRANCH
}

if ! git diff --no-index .github -- $tmpdir/ci/.github &> /dev/null; then
    echo ">>> Workflow has changed, pulling in"
    update
fi

master_commit=$(git log -1 --format=%H origin/$ORIGIN_BRANCH)
workflow_commit=$(git log -1 --format=%H origin/${WORKFLOW_BRANCH}^)
echo ">>> Assert master/workflow commits are same: $master_commit $workflow_commit"
if [[ "$master_commit" != "$workflow_commit" ]]; then
    echo ">>> Updating workflow"
    update
fi

echo ">>> Done Exit"
