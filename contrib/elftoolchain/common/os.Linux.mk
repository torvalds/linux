#
# Build recipes for Linux based operating systems.
#
# $Id: os.Linux.mk 3594 2018-04-11 18:26:50Z jkoshy $

_NATIVE_ELF_FORMAT = native-elf-format

.if !make(obj)
.BEGIN:	${.OBJDIR}/${_NATIVE_ELF_FORMAT}.h

${.OBJDIR}/${_NATIVE_ELF_FORMAT}.h:
	${.CURDIR}/${_NATIVE_ELF_FORMAT} > ${.TARGET} || rm ${.TARGET}
.endif

CLEANFILES += ${.OBJDIR}/${_NATIVE_ELF_FORMAT}.h
