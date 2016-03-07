#
# pandoc thinks that both "_" needs to be escaped.  Remove the extra
# backslashes.  Also put in proper backquotes now that pandoc won't quote
# them.
#
s/$bq/`/g
s/\\_/_/g
#
# Unwrap docproc directives.
#
s/^``DOCPROC: !E\(.*\)``$/.. kernel-doc:: \1\n   :export:/
s/^``DOCPROC: !I\(.*\)``$/.. kernel-doc:: \1\n   :internal:/
s/^``DOCPROC: !F\([^ ]*\) \(.*\)``$/.. kernel-doc:: \1\n   :functions: \2/
s/^``DOCPROC: !P\([^ ]*\) \(.*\)``$/.. kernel-doc:: \1\n   :doc: \2/
s/^``DOCPROC: \(!.*\)``$/.. WARNING: DOCPROC directive not supported: \1/
#
# Trim trailing whitespace.
#
s/[[:space:]]*$//
