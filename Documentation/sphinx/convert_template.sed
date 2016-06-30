#
# Pandoc doesn't grok <function> or <structname>, so convert them
# ahead of time.
#
# Use the following escapes to pass through pandoc:
#	$bq = "`"
#	$lt = "<"
#	$gt = ">"
#
s%<function>\([^<(]\+\)()</function>%:c:func:$bq\1()$bq%g
s%<function>\([^<(]\+\)</function>%:c:func:$bq\1()$bq%g
s%<structname>struct *\([^<]\+\)</structname>%:c:type:$bqstruct \1 $lt\1$gt$bq%g
s%struct <structname>\([^<]\+\)</structname>%:c:type:$bqstruct \1 $lt\1$gt$bq%g
s%<structname>\([^<]\+\)</structname>%:c:type:$bqstruct \1 $lt\1$gt$bq%g
#
# Wrap docproc directives in para and code blocks.
#
s%^\(!.*\)$%<para><code>DOCPROC: \1</code></para>%
