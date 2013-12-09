#!/bin/sh

# Generate system call probe description macros from syscall metadata dump file.
# The resulting header will be written in the headers subdirectory, in a file name
# based on the name of the input file.
#
# example usage:
#
# lttng-syscalls-generate-headers.sh <type> <input_dir> <input_filename_in_dir> <bitness>
# lttng-syscalls-generate-headers.sh integers 3.0.4 x86-64-syscalls-3.0.4 64
# lttng-syscalls-generate-headers.sh pointers 3.0.4 x86-64-syscalls-3.0.4 64

CLASS=$1
INPUTDIR=$2
INPUTFILE=$3
BITNESS=$4
INPUT=${INPUTDIR}/${INPUTFILE}
SRCFILE=gen.tmp.0
TMPFILE=gen.tmp.1
HEADER=headers/${INPUTFILE}_${CLASS}.h

cp ${INPUT} ${SRCFILE}

#Cleanup
perl -p -e 's/^\[.*\] //g' ${SRCFILE} > ${TMPFILE}
mv ${TMPFILE} ${SRCFILE}

perl -p -e 's/^syscall sys_([^ ]*)/syscall $1/g' ${SRCFILE} > ${TMPFILE}
mv ${TMPFILE} ${SRCFILE}

#Filter

if [ "$CLASS" = integers ]; then
	#select integers and no-args.
	CLASSCAP=INTEGERS
	grep -v "\\*\|cap_user_header_t" ${SRCFILE} > ${TMPFILE}
	mv ${TMPFILE} ${SRCFILE}
fi


if [ "$CLASS" = pointers ]; then
	#select system calls using pointers.
	CLASSCAP=POINTERS
	grep "\\*\|cap_#user_header_t" ${SRCFILE} > ${TMPFILE}
	mv ${TMPFILE} ${SRCFILE}
fi

echo "/* THIS FILE IS AUTO-GENERATED. DO NOT EDIT */" > ${HEADER}

echo \
"#ifndef CREATE_SYSCALL_TABLE

#if !defined(_TRACE_SYSCALLS_${CLASSCAP}_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SYSCALLS_${CLASSCAP}_H

#include <linux/tracepoint.h>
#include <linux/syscalls.h>
#include \"${INPUTFILE}_${CLASS}_override.h\"
#include \"syscalls_${CLASS}_override.h\"
" >> ${HEADER}

if [ "$CLASS" = integers ]; then

NRARGS=0

printf \
'SC_DECLARE_EVENT_CLASS_NOARGS(syscalls_noargs,\n'\
'	TP_STRUCT__entry(),\n'\
'	TP_fast_assign(),\n'\
'	TP_printk()\n'\
')'\
	>> ${HEADER}

grep "^syscall [^ ]* nr [^ ]* nbargs ${NRARGS} " ${SRCFILE} > ${TMPFILE}
perl -p -e 's/^syscall ([^ ]*) nr ([^ ]*) nbargs ([^ ]*) '\
'types: \(([^)]*)\) '\
'args: \(([^)]*)\)/'\
'#ifndef OVERRIDE_'"${BITNESS}"'_sys_$1\n'\
'SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_$1)\n'\
'#endif/g'\
	${TMPFILE} >> ${HEADER}

fi


# types: 4
# args   5

NRARGS=1
grep "^syscall [^ ]* nr [^ ]* nbargs ${NRARGS} " ${SRCFILE} > ${TMPFILE}
perl -p -e 's/^syscall ([^ ]*) nr ([^ ]*) nbargs ([^ ]*) '\
'types: \(([^)]*)\) '\
'args: \(([^)]*)\)/'\
'#ifndef OVERRIDE_'"${BITNESS}"'_sys_$1\n'\
'SC_TRACE_EVENT(sys_$1,\n'\
'	TP_PROTO($4 $5),\n'\
'	TP_ARGS($5),\n'\
'	TP_STRUCT__entry(__field($4, $5)),\n'\
'	TP_fast_assign(tp_assign($4, $5, $5)),\n'\
'	TP_printk()\n'\
')\n'\
'#endif/g'\
	${TMPFILE} >> ${HEADER}

# types: 4 5
# args   6 7

NRARGS=2
grep "^syscall [^ ]* nr [^ ]* nbargs ${NRARGS} " ${SRCFILE} > ${TMPFILE}
perl -p -e 's/^syscall ([^ ]*) nr ([^ ]*) nbargs ([^ ]*) '\
'types: \(([^,]*), ([^)]*)\) '\
'args: \(([^,]*), ([^)]*)\)/'\
'#ifndef OVERRIDE_'"${BITNESS}"'_sys_$1\n'\
'SC_TRACE_EVENT(sys_$1,\n'\
'	TP_PROTO($4 $6, $5 $7),\n'\
'	TP_ARGS($6, $7),\n'\
'	TP_STRUCT__entry(__field($4, $6) __field($5, $7)),\n'\
'	TP_fast_assign(tp_assign($4, $6, $6) tp_assign($5, $7, $7)),\n'\
'	TP_printk()\n'\
')\n'\
'#endif/g'\
	${TMPFILE} >> ${HEADER}

# types: 4 5 6
# args   7 8 9

NRARGS=3
grep "^syscall [^ ]* nr [^ ]* nbargs ${NRARGS} " ${SRCFILE} > ${TMPFILE}
perl -p -e 's/^syscall ([^ ]*) nr ([^ ]*) nbargs ([^ ]*) '\
'types: \(([^,]*), ([^,]*), ([^)]*)\) '\
'args: \(([^,]*), ([^,]*), ([^)]*)\)/'\
'#ifndef OVERRIDE_'"${BITNESS}"'_sys_$1\n'\
'SC_TRACE_EVENT(sys_$1,\n'\
'	TP_PROTO($4 $7, $5 $8, $6 $9),\n'\
'	TP_ARGS($7, $8, $9),\n'\
'	TP_STRUCT__entry(__field($4, $7) __field($5, $8) __field($6, $9)),\n'\
'	TP_fast_assign(tp_assign($4, $7, $7) tp_assign($5, $8, $8) tp_assign($6, $9, $9)),\n'\
'	TP_printk()\n'\
')\n'\
'#endif/g'\
	${TMPFILE} >> ${HEADER}


# types: 4 5  6  7
# args   8 9 10 11

NRARGS=4
grep "^syscall [^ ]* nr [^ ]* nbargs ${NRARGS} " ${SRCFILE} > ${TMPFILE}
perl -p -e 's/^syscall ([^ ]*) nr ([^ ]*) nbargs ([^ ]*) '\
'types: \(([^,]*), ([^,]*), ([^,]*), ([^)]*)\) '\
'args: \(([^,]*), ([^,]*), ([^,]*), ([^)]*)\)/'\
'#ifndef OVERRIDE_'"${BITNESS}"'_sys_$1\n'\
'SC_TRACE_EVENT(sys_$1,\n'\
'	TP_PROTO($4 $8, $5 $9, $6 $10, $7 $11),\n'\
'	TP_ARGS($8, $9, $10, $11),\n'\
'	TP_STRUCT__entry(__field($4, $8) __field($5, $9) __field($6, $10) __field($7, $11)),\n'\
'	TP_fast_assign(tp_assign($4, $8, $8) tp_assign($5, $9, $9) tp_assign($6, $10, $10) tp_assign($7, $11, $11)),\n'\
'	TP_printk()\n'\
')\n'\
'#endif/g'\
	${TMPFILE} >> ${HEADER}

# types: 4  5  6  7  8
# args   9 10 11 12 13

NRARGS=5
grep "^syscall [^ ]* nr [^ ]* nbargs ${NRARGS} " ${SRCFILE} > ${TMPFILE}
perl -p -e 's/^syscall ([^ ]*) nr ([^ ]*) nbargs ([^ ]*) '\
'types: \(([^,]*), ([^,]*), ([^,]*), ([^,]*), ([^)]*)\) '\
'args: \(([^,]*), ([^,]*), ([^,]*), ([^,]*), ([^)]*)\)/'\
'#ifndef OVERRIDE_'"${BITNESS}"'_sys_$1\n'\
'SC_TRACE_EVENT(sys_$1,\n'\
'	TP_PROTO($4 $9, $5 $10, $6 $11, $7 $12, $8 $13),\n'\
'	TP_ARGS($9, $10, $11, $12, $13),\n'\
'	TP_STRUCT__entry(__field($4, $9) __field($5, $10) __field($6, $11) __field($7, $12) __field($8, $13)),\n'\
'	TP_fast_assign(tp_assign($4, $9, $9) tp_assign($5, $10, $10) tp_assign($6, $11, $11) tp_assign($7, $12, $12) tp_assign($8, $13, $13)),\n'\
'	TP_printk()\n'\
')\n'\
'#endif/g'\
	${TMPFILE} >> ${HEADER}


# types: 4   5  6  7  8  9
# args   10 11 12 13 14 15

NRARGS=6
grep "^syscall [^ ]* nr [^ ]* nbargs ${NRARGS} " ${SRCFILE} > ${TMPFILE}
perl -p -e 's/^syscall ([^ ]*) nr ([^ ]*) nbargs ([^ ]*) '\
'types: \(([^,]*), ([^,]*), ([^,]*), ([^,]*), ([^,]*), ([^\)]*)\) '\
'args: \(([^,]*), ([^,]*), ([^,]*), ([^,]*), ([^,]*), ([^\)]*)\)/'\
'#ifndef OVERRIDE_'"${BITNESS}"'_sys_$1\n'\
'SC_TRACE_EVENT(sys_$1,\n'\
'	TP_PROTO($4 $10, $5 $11, $6 $12, $7 $13, $8 $14, $9 $15),\n'\
'	TP_ARGS($10, $11, $12, $13, $14, $15),\n'\
'	TP_STRUCT__entry(__field($4, $10) __field($5, $11) __field($6, $12) __field($7, $13) __field($8, $14) __field($9, $15)),\n'\
'	TP_fast_assign(tp_assign($4, $10, $10) tp_assign($5, $11, $11) tp_assign($6, $12, $12) tp_assign($7, $13, $13) tp_assign($8, $14, $14) tp_assign($9, $15, $15)),\n'\
'	TP_printk()\n'\
')\n'\
'#endif/g'\
	${TMPFILE} >> ${HEADER}

# Macro for tracing syscall table

rm -f ${TMPFILE}
for NRARGS in $(seq 0 6); do
	grep "^syscall [^ ]* nr [^ ]* nbargs ${NRARGS} " ${SRCFILE} >> ${TMPFILE}
done

echo \
"
#endif /*  _TRACE_SYSCALLS_${CLASSCAP}_H */

/* This part must be outside protection */
#include \"../../../probes/define_trace.h\"

#else /* CREATE_SYSCALL_TABLE */

#include \"${INPUTFILE}_${CLASS}_override.h\"
#include \"syscalls_${CLASS}_override.h\"
" >> ${HEADER}

NRARGS=0

if [ "$CLASS" = integers ]; then
#noargs
grep "^syscall [^ ]* nr [^ ]* nbargs ${NRARGS} " ${SRCFILE} > ${TMPFILE}
perl -p -e 's/^syscall ([^ ]*) nr ([^ ]*) nbargs ([^ ]*) .*$/'\
'#ifndef OVERRIDE_TABLE_'"${BITNESS}"'_sys_$1\n'\
'TRACE_SYSCALL_TABLE\(syscalls_noargs, sys_$1, $2, $3\)\n'\
'#endif/g'\
	${TMPFILE} >> ${HEADER}
fi

#others.
grep -v "^syscall [^ ]* nr [^ ]* nbargs ${NRARGS} " ${SRCFILE} > ${TMPFILE}
perl -p -e 's/^syscall ([^ ]*) nr ([^ ]*) nbargs ([^ ]*) .*$/'\
'#ifndef OVERRIDE_TABLE_'"${BITNESS}"'_sys_$1\n'\
'TRACE_SYSCALL_TABLE(sys_$1, sys_$1, $2, $3)\n'\
'#endif/g'\
	${TMPFILE} >> ${HEADER}

echo -n \
"
#endif /* CREATE_SYSCALL_TABLE */
" >> ${HEADER}

#fields names: ...char * type with *name* or *file* or *path* or *root*
# or *put_old* or *type*
cp -f ${HEADER} ${TMPFILE}
rm -f ${HEADER}
perl -p -e 's/__field\(([^,)]*char \*), ([^\)]*)(name|file|path|root|put_old|type)([^\)]*)\)/__string_from_user($2$3$4, $2$3$4)/g'\
	${TMPFILE} >> ${HEADER}
cp -f ${HEADER} ${TMPFILE}
rm -f ${HEADER}
perl -p -e 's/tp_assign\(([^,)]*char \*), ([^,]*)(name|file|path|root|put_old|type)([^,]*), ([^\)]*)\)/tp_copy_string_from_user($2$3$4, $5)/g'\
	${TMPFILE} >> ${HEADER}

#prettify addresses heuristics.
#field names with addr or ptr
cp -f ${HEADER} ${TMPFILE}
rm -f ${HEADER}
perl -p -e 's/__field\(([^,)]*), ([^,)]*addr|[^,)]*ptr)([^),]*)\)/__field_hex($1, $2$3)/g'\
	${TMPFILE} >> ${HEADER}

#field types ending with '*'
cp -f ${HEADER} ${TMPFILE}
rm -f ${HEADER}
perl -p -e 's/__field\(([^,)]*\*), ([^),]*)\)/__field_hex($1, $2)/g'\
	${TMPFILE} >> ${HEADER}

#strip the extra type information from tp_assign.
cp -f ${HEADER} ${TMPFILE}
rm -f ${HEADER}
perl -p -e 's/tp_assign\(([^,)]*), ([^,]*), ([^\)]*)\)/tp_assign($2, $3)/g'\
	${TMPFILE} >> ${HEADER}

rm -f ${INPUTFILE}.tmp
rm -f ${TMPFILE}
rm -f ${SRCFILE}
