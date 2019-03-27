STR1=
STR2=	a
STR3=	ab
STR4=	abc
STR5=	abcd
STR6=	abcde
STR7=	abcdef
STR8=	abcdefghijklmnopqrstuvwxyz

all:
	@echo ${STR1:hash}
	@echo ${STR2:hash}
	@echo ${STR3:hash}
	@echo ${STR4:hash}
	@echo ${STR5:hash}
	@echo ${STR6:hash}
	@echo ${STR7:hash}
	@echo ${STR8:hash}
