BYECMD		= echo bye
LATERCMD	= echo later
TEST1 :sh	= echo hello
TEST2 :sh	= ${BYECMD}
TEST3		= ${LATERCMD:sh}

all:
	@echo "TEST1=${TEST1}"
	@echo "TEST2=${TEST2}"
	@echo "TEST3=${TEST3}"
