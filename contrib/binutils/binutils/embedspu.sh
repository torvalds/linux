#! /bin/sh 
# Embed an SPU ELF executable into a PowerPC object file.
#
# Copyright 2006, 2007 Free Software Foundation, Inc.
#
# This file is part of GNU Binutils.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
# 02110-1301, USA.

usage ()
{
  echo "Usage: embedspu [flags] symbol_name input_filename output_filename"
  echo
  echo "        input_filename:  SPU ELF executable to be embedded"
  echo "        output_filename: Resulting PowerPC object file"
  echo "        symbol_name:     Name of program handle struct to be defined"
  echo "        flags:           GCC flags defining PowerPC object file format"
  echo "                         (e.g. -m32 or -m64)"
  exit 1
}

program_transform_name=
mydir=`dirname "$0"`

find_prog ()
{
  prog=`echo $1 | sed "$program_transform_name"`
  prog="$mydir/$prog"
  test -x "$prog" && return 0
  prog="$mydir/$1"
  test -x "$prog" && return 0
  prog=`echo $1 | sed "$program_transform_name"`
  which $prog > /dev/null 2> /dev/null && return 0
  return 1
}

SYMBOL=
INFILE=
OUTFILE=
FLAGS=

parse_args ()
{
  while test -n "$1"; do
    case "$1" in
      -*) FLAGS="${FLAGS} $1" ;;
      *)  if test -z "$SYMBOL"; then
	    SYMBOL="$1"
	  elif test -z "$INFILE"; then
	    INFILE="$1"
	  elif test -z "$OUTFILE"; then
	    OUTFILE="$1"
	  else
	    echo "Too many arguments!"
	    usage
	  fi ;;
    esac
    shift
  done
  if test -z "$OUTFILE"; then
    usage
  fi
  if test ! -r "$INFILE"; then
    echo "${INFILE}: File not found"
    usage
  fi
}

main ()
{
  parse_args "$@"

  # Find a powerpc gcc.  Support running from a combined tree build.
  if test -x "$mydir/../gcc/xgcc"; then
    CC="$mydir/../gcc/xgcc -B$mydir/../gcc/"
  else
    find_prog gcc
    if test $? -ne 0; then
      echo "Cannot find $prog"
      exit 1
    fi
    CC="$prog"
  fi

  # Find readelf.  Any old readelf should do.
  find_prog readelf
  if test $? -ne 0; then
    if which readelf > /dev/null 2> /dev/null; then
      prog=readelf
    else
      echo "Cannot find $prog"
      exit 1
    fi
  fi
  READELF="$prog"

  # Sanity check the input file
  if ! ${READELF} -h ${INFILE} | grep 'Class:.*ELF32' >/dev/null 2>/dev/null \
     || ! ${READELF} -h ${INFILE} | grep 'Type:.*EXEC' >/dev/null 2>/dev/null \
     || ! ${READELF} -h ${INFILE} | egrep 'Machine:.*(SPU|17)' >/dev/null 2>/dev/null
  then
    echo "${INFILE}: Does not appear to be an SPU executable"
    exit 1
  fi

  toe=`${READELF} -S ${INFILE} | sed -n -e 's, *\[ *\([0-9]*\)\] *\.toe *[PROGN]*BITS *\([0-9a-f]*\).*,\1 \2,p'`
  toe_addr=`echo $toe | sed -n -e 's,.* ,,p'`
  toe=`echo $toe | sed -n -e 's, .*,,p'`
  # For loaded sections, pick off section number, address, and file offset
  sections=`${READELF} -S ${INFILE} | sed -n -e 's, *\[ *\([0-9]*\)\] *[^ ]* *PROGBITS *\([0-9a-f]*\) *\([0-9a-f]*\).*,\1 \2 \3,p'`
  sections=`echo ${sections}`
  # For relocation sections, pick off file offset and info (points to
  # section where relocs apply)
  relas=`${READELF} -S ${INFILE} | sed -n -e 's, *\[ *[0-9]*\] *[^ ]* *RELA *[0-9a-f]* *0*\([0-9a-f][0-9a-f]*\).* \([0-9a-f][0-9a-f]*\) *[0-9a-f][0-9a-f]*$,\1 \2,p'`
  relas=`echo ${relas}`

  # Build embedded SPU image.
  # 1. The whole SPU ELF file is written to .rodata.speelf
  # 2. Symbols starting with the string "_EAR_" in the SPU ELF image are
  #    special.  They allow an SPU program to access corresponding symbols
  #    (ie. minus the _EAR_ prefix), in the PowerPC program.  _EAR_ without
  #    a suffix is used to refer to the addrress of the SPU image in
  #    PowerPC address space.  _EAR_* symbols must all be defined in .toe
  #    at 16 byte intervals, or they must be defined in other non-bss
  #    sections.
  #    Find all _EAR_ symbols in .toe using readelf, sort by address, and
  #    write the address of the corresponding PowerPC symbol in a table
  #    built in .data.spetoe.  For _EAE_ symbols not in .toe, create
  #    .reloc commands to relocate their location directly.
  # 3. Look for R_SPU_PPU32 and R_SPU_PPU64 relocations in the SPU ELF image
  #    and create .reloc commands for them.
  # 4. Write a struct spe_program_handle to .data.
  # 5. Write a table of _SPUEAR_ symbols.
  ${CC} ${FLAGS} -x assembler-with-cpp -nostartfiles -nostdlib \
	-Wa,-mbig -Wl,-r -Wl,-x -o ${OUTFILE} - <<EOF
 .section .data.spetoe,"aw",@progbits
 .p2align 7
__spetoe__:
`${READELF} -s -W ${INFILE} | grep ' _EAR_' | sort -k 2 | awk \
'BEGIN { \
	addr = strtonum ("0x" "'${toe_addr-0}'"); \
	split ("'"${sections}"'", s, " "); \
	for (i = 1; i in s; i += 3) { \
	    sec_off[s[i]] = strtonum ("0x" s[i+2]) - strtonum ("0x" s[i+1]); \
	} \
} \
$7 == "'${toe}'" && strtonum ("0x" $2) != addr { \
	print "#error Symbol " $8 " not in 16 byte element toe array!"; \
} \
$7 == "'${toe}'" { \
	addr = addr + 16; \
} \
$7 == "'${toe}'" { \
	print "#ifdef _LP64"; \
	print " .quad " ($8 == "_EAR_" ? "__speelf__" : substr($8, 6)) ", 0"; \
	print "#else"; \
	print " .int 0, " ($8 == "_EAR_" ? "__speelf__" : substr($8, 6)) ", 0, 0"; \
	print "#endif"; \
} \
$7 != "'${toe}'" && $7 in sec_off { \
	print "#ifdef _LP64"; \
	print " .reloc __speelf__+" strtonum ("0x" $2) + sec_off[$7] ", R_PPC64_ADDR64, " ($8 == "_EAR_" ? "__speelf__" : substr($8, 6)); \
	print "#else"; \
	print " .reloc __speelf__+" strtonum ("0x" $2) + sec_off[$7] + 4 ", R_PPC_ADDR32, " ($8 == "_EAR_" ? "__speelf__" : substr($8, 6)); \
	print "#endif"; \
	if (!donedef) { print "#define HAS_RELOCS 1"; donedef = 1; }; \
} \
$7 != "'${toe}'" && ! $7 in sec_off { \
	print "#error Section not found for " $8; \
} \
'`
`test -z "${relas}" || ${READELF} -r -W ${INFILE} | awk \
'BEGIN { \
	split ("'"${sections}"'", s, " "); \
	for (i = 1; i in s; i += 3) { \
	    sec_off[s[i]] = strtonum ("0x" s[i+2]) - strtonum ("0x" s[i+1]); \
	} \
	split ("'"${relas}"'", s, " "); \
	for (i = 1; i in s; i += 2) { \
	    rela[s[i]] = strtonum (s[i+1]); \
	} \
} \
/^Relocation section/ { \
	sec = substr($6, 3); \
} \
$3 ~ /R_SPU_PPU/ { \
	print "#ifdef _LP64"; \
	print " .reloc __speelf__+" strtonum ("0x" $1) + sec_off[rela[sec]] ", R_PPC64_ADDR" substr($3, 10) ", " ($5 != "" ? $5 "+0x" $7 : "__speelf__ + 0x" $4); \
	print "#else"; \
	print " .reloc __speelf__+" strtonum ("0x" $1) + sec_off[rela[sec]] + (substr($3, 10) == "64" ? 4 : 0)", R_PPC_ADDR32, " ($5 != "" ? $5 "+0x" $7 : "__speelf__ + 0x" $4); \
	print "#endif"; \
	if (!donedef) { print "#define HAS_RELOCS 1"; donedef = 1; }; \
} \
$3 ~ /unrecognized:/ { \
	print "#ifdef _LP64"; \
	print " .reloc __speelf__+" strtonum ("0x" $1) + sec_off[rela[sec]] ", R_PPC64_ADDR" ($4 == "f" ? "64" : "32") ", " ($6 != "" ? $6 "+0x" $8 : "__speelf__ + 0x" $5); \
	print "#else"; \
	print " .reloc __speelf__+" strtonum ("0x" $1) + sec_off[rela[sec]] + ($4 == "f" ? 4 : 0)", R_PPC_ADDR32, " ($6 != "" ? $6 "+0x" $8 : "__speelf__ + 0x" $5); \
	print "#endif"; \
	if (!donedef) { print "#define HAS_RELOCS 1"; donedef = 1; }; \
} \
'`
#if defined (HAS_RELOCS) && (defined (__PIC__) || defined (__PIE__))
 .section .data.rel.ro.speelf,"a",@progbits
#else
 .section .rodata.speelf,"a",@progbits
#endif
 .p2align 7
__speelf__:
 .incbin "${INFILE}"

 .section .data,"aw",@progbits
 .globl ${SYMBOL}
 .type ${SYMBOL}, @object
# fill in a struct spe_program_handle
#ifdef _LP64
 .p2align 3
${SYMBOL}:
 .int 24
 .int 0
 .quad __speelf__
 .quad __spetoe__
#else
 .p2align 2
${SYMBOL}:
 .int 12
 .int __speelf__
 .int __spetoe__
#endif
 .size ${SYMBOL}, . - ${SYMBOL}

`${READELF} -s -W ${INFILE} | grep ' _SPUEAR_' | sort -k 2 | awk \
'{ \
	print " .globl '${SYMBOL}'_" substr($8, 9); \
	print " .type '${SYMBOL}'_" substr($8, 9) ", @object"; \
	print " .size '${SYMBOL}'_" substr($8, 9) ", 4"; \
	print "'${SYMBOL}'_" substr($8, 9) ":"; \
	print " .int 0x" $2; \
} \
'`
EOF
}

main "$@"
