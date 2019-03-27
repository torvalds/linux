# $Id: moderrs.mk,v 1.1.1.1 2014/08/30 18:57:18 sjg Exp $
#
# various modifier error tests

VAR=TheVariable
# incase we have to change it ;-)
MOD_UNKN=Z
MOD_TERM=S,V,v
MOD_S:= ${MOD_TERM},

all:	modunkn modunknV varterm vartermV modtermV

modunkn:
	@echo "Expect: Unknown modifier 'Z'"
	@echo "VAR:Z=${VAR:Z}"

modunknV:
	@echo "Expect: Unknown modifier 'Z'"
	@echo "VAR:${MOD_UNKN}=${VAR:${MOD_UNKN}}"

varterm:
	@echo "Expect: Unclosed variable specification for VAR"
	@echo VAR:S,V,v,=${VAR:S,V,v,

vartermV:
	@echo "Expect: Unclosed variable specification for VAR"
	@echo VAR:${MOD_TERM},=${VAR:${MOD_S}

modtermV:
	@echo "Expect: Unclosed substitution for VAR (, missing)"
	-@echo "VAR:${MOD_TERM}=${VAR:${MOD_TERM}}"
