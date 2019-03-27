# $Id: export-env.mk,v 1.1.1.2 2016/02/18 20:35:24 sjg Exp $

# our normal .export, subsequent changes affect the environment
UT_TEST=this
.export UT_TEST
UT_TEST:= ${.PARSEFILE}

# not so with .export-env
UT_ENV=exported
.export-env UT_ENV
UT_ENV=not-exported

# gmake style export goes further; affects nothing but the environment
UT_EXP=before-export
export UT_EXP=exported
UT_EXP=not-exported

UT_LIT= literal ${UT_TEST}
.export-literal UT_LIT

all:
	@echo make:; ${UT_TEST UT_ENV UT_EXP UT_LIT:L:@v@echo $v=${$v};@}
	@echo env:; ${UT_TEST UT_ENV UT_EXP UT_LIT:L:@v@echo $v=$${$v};@}




