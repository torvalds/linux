# options for all the directories below

COPTS+=-Oz -fno-stack-protector
COPTS+=-fno-unwind-tables -fno-asynchronous-unwind-tables

.if ${MACHINE} == "amd64"
COPTS+=-fcf-protection=none -fno-ret-clean
.endif
.if ${MACHINE} == "arm64"
COPTS+=-mbranch-protection=none
.endif

MAN=
LDSTATIC=-static
NOPIE=
NOMAN=1
