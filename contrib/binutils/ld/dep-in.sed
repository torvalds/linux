:loop
/\\$/N
/\\$/b loop

s! \./! !g
s!@INCDIR@!$(INCDIR)!g
s!@TOPDIR@/include!$(INCDIR)!g
s!@BFDDIR@!$(BFDDIR)!g
s!@TOPDIR@/bfd!$(BFDDIR)!g
s!@SRCDIR@/!!g
s! \.\./bfd/hosts/[^ ]*\.h! !g
s! \.\./intl/libintl\.h!!g

s/\\\n */ /g

s/ *$//
s/  */ /g
/:$/d

s/\(.\{50\}[^ ]*\) /\1 \\\
  /g
