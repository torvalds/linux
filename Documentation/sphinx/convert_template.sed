#
# Pandoc doesn't grok <function> or <structname>, so convert them
# ahead of time.
#
# Use "$bq" instead of "`" so that pandoc won't mess with it.
#
s%<function>\([^<(]\+\)()</function>%:c:func:$bq\1()$bq%g
s%<function>\([^<(]\+\)</function>%:c:func:$bq\1()$bq%g
s%<structname>struct *\([^<]\+\)</structname>%:c:type:$bq\1$bq%g
s%<structname>\([^<]\+\)</structname>%:c:type:$bq\1$bq%g
#
# Wrap docproc directives in para and code blocks.
#
s%^\(!.*\)$%<para><code>DOCPROC: \1</code></para>%
