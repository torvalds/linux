# $FreeBSD$

usage()
{
    cat <<__EOF__ >&2
Generate ATF test cases from a set of DTrace tests.

usage: sh $(basename $0) [-e <excludes>] <category> [<testfiles>]

  excludes:     A shell script which defines test cases that are to be skipped,
                or aren't expected to pass.
  category:     The test category, in the form of <arch>/<feature>. For example,
                "common/aggs" is the test category for D aggregations.
  testfiles:    The test files for the tests in the specified category.
__EOF__
    exit 1
}

gentestcase()
{
    local mod tcase tfile

    tfile=$1
    tcase=$2
    mod=$3

    cat <<__EOF__
atf_test_case $tcase
${tcase}_head()
{
    atf_set 'descr' 'DTrace test ${CATEGORY}/${tfile}'
}
${tcase}_body()
{
    $mod
    atf_check -s exit:0 -o empty -e empty \\
        "\$(atf_get_srcdir)/../../dtest" "\$(atf_get_srcdir)/${tfile}"
}
__EOF__
}

gentestcases()
{
    local mod tcase tfile tfiles

    eval tfiles=\$$1
    mod=$2

    for tfile in ${tfiles}; do
        case $tfile in
        drp.*.d|err.*.d|tst.*.d|*.ksh)
            # Test names need to be mangled for ATF.
            tcase=$(echo "$tfile" | tr '.-' '_')
            gentestcase "$tfile" "$tcase" "$mod"
            TCASES="$TCASES $tcase"
            ;;
        esac
    done
}

set -e

#
# Parse arguments.
#
case $1 in
-e)
    shift; EXCLUDES=$1; shift
    ;;
esac

CATEGORY=$1
shift
if ! expr "$CATEGORY" : '[^/]*/[^/]*' >/dev/null 2>&1; then
    usage
fi
FEATURE=$(basename ${CATEGORY})
ARCH=$(dirname ${CATEGORY})

#
# Remove skipped tests and expected failures from the main test list.
#
. $EXCLUDES
EXFAILS=$(echo -e "$EXFAIL" | grep "^${CATEGORY}/" | xargs basename -a)
SKIPS=$(echo -e "$SKIP" | grep "^${CATEGORY}/" | xargs basename -a)

FILELIST=$(mktemp)
trap 'rm -f $FILELIST' EXIT

echo "$@" | tr ' ' '\n' | xargs basename -a | sort > ${FILELIST}
TFILES=$(printf '%s\n%s' "$EXFAILS" "$SKIPS" | sort | comm -13 /dev/stdin $FILELIST)

#
# Generate test cases.
#
gentestcases SKIPS "atf_skip \"test may hang or cause system instability\""
gentestcases EXFAILS "atf_expect_fail \"test is known to fail\""
gentestcases TFILES

#
# Generate the test init function.
#
cat <<__EOF__
atf_init_test_cases()
{
$(for tcase in ${TCASES}; do echo "    atf_add_test_case $tcase"; done)
}
__EOF__

rm -f $FILELIST
