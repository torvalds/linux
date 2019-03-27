
all:
	@for x in "" A= A=42; do ${.MAKE} -f ${MAKEFILE} show $$x; done

show:
	@echo "The answer is ${A:?known:unknown}"
	@echo "The answer is ${A:?$A:unknown}"
	@echo "The answer is ${empty(A):?empty:$A}"
