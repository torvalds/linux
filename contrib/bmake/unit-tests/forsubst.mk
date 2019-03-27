# $Id: forsubst.mk,v 1.1.1.1 2014/08/30 18:57:18 sjg Exp $

all: for-subst

here := ${.PARSEDIR}
# this should not run foul of the parser
.for file in ${.PARSEFILE}
for-subst:	  ${file:S;^;${here}/;g}
	@echo ".for with :S;... OK"
.endfor
