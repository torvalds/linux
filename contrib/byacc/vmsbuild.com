$! $Id: vmsbuild.com,v 1.2 2014/04/06 19:08:57 tom Exp $
$! VMS build-script for BYACC.  Requires installed C compiler
$!
$! Screen Configurations
$! ---------------------
$! To build BYACC, type:
$!        $ @vmsbuild [BYACC [<compiler> [bld_target]]]
$!
$! where:
$!        <compiler> :== { decc | vaxc }
$!
$! The default compiler on VAX hosts is vaxc, else decc (Alpha hosts).
$!
$! -----------------------------------------------------------
$ hlp = f$edit("''p1'", "UPCASE")
$ if "''hlp'" .eqs. "HELP" .or. -
        "''hlp'" .eqs. "-H" .or. -
                "''hlp'" .eqs. "-?" .or. -
                        "''hlp'" .eqs. "?" then gosub usage
$ goto start
$!
$ vaxc_config:
$    comp       = "__vaxc__=1"
$    CFLAGS     = "/VAXC"
$    DEFS       = ",HAVE_STRERROR"
$    using_vaxc = 1
$    return
$!
$ decc_config:
$    comp   = "__decc__=1"
$    CFLAGS = "/DECC/prefix=all"
$    DEFS   = ",HAVE_ALARM,HAVE_STRERROR"
$    return
$!
$ usage:
$    write sys$output "usage: "
$    write sys$output "      $ @vmsbuild [BYACC [{decc | vaxc} [<bldtarget>]]]"
$    exit 2
$!
$ start:
$! -----------------------------------------------------------
$! pickup user's compiler choice, if any
$! -----------------------------------------------------------
$!
$ comp = ""
$ using_vaxc = 0
$ if "''p2'" .nes. "" 
$ then
$    comp = f$edit(p2, "UPCASE")
$    if "''comp'" .eqs. "VAXC"
$    then
$        gosub vaxc_config
$    else
$        if "''comp'" .eqs. "DECC"
$        then
$            gosub decc_config
$        else
$            gosub usage
$        endif
$    endif
$ endif
$! -----------------------------------------------------------
$!      Build the option-file
$!
$ open/write optf vms_link.opt
$ write optf "closure.obj"
$ write optf "error.obj"
$ write optf "lalr.obj"
$ write optf "lr0.obj"
$ write optf "mkpar.obj"
$ write optf "output.obj"
$ write optf "reader.obj"
$ write optf "yaccpar.obj"
$ write optf "symtab.obj"
$ write optf "verbose.obj"
$ write optf "warshall.obj"
$! ----------------------------------
$! Look for the compiler used and specify architecture.
$!
$ CC = "CC"
$ if f$getsyi("HW_MODEL").ge.1024
$ then
$  arch = "__alpha__=1"
$  if "''comp'" .eqs. "" then gosub decc_config
$ else
$  arch = "__vax__=1"
$  if "''comp'" .nes. "" then goto screen_config
$  if f$search("SYS$SYSTEM:VAXC.EXE").nes.""
$  then
$   gosub vaxc_config
$  else
$   if f$search("SYS$SYSTEM:DECC$COMPILER.EXE").nes.""
$   then
$    gosub decc_config
$   else
$    DEFS = ",HAVE_STRERROR"
$    if f$trnlnm("GNU_CC").eqs.""
$    then
$     write sys$output "C compiler required to rebuild BYACC"
$     close optf
$     exit
$    else
$     write optf "gnu_cc:[000000]gcclib.olb/lib"
$     comp = "__gcc__=1"
$     CC = "GCC"
$    endif
$   endif
$  endif
$ endif
$!
$ screen_config:
$!
$ if using_vaxc .eq. 1 then write optf "sys$library:vaxcrtl.exe/share"
$ close optf
$! -------------- vms_link.opt is created -------------
$ if f$edit("''p1'", "UPCASE") .eqs. "VMS_LINK.OPT"
$ then
$!  mms called this script to build vms_link.opt.  all done
$   exit
$ endif
$!
$ if f$search("SYS$SYSTEM:MMS.EXE").eqs.""
$ then
$!  can also use /Debug /Listing, /Show=All
$
$   CFLAGS := 'CFLAGS/Diagnostics /Define=("''DEFS'") /Include=([])
$
$  	if "''p3'" .nes. "" then goto 'p3
$!
$!
$ all :
$!
$	call make closure
$	call make error
$	call make lalr
$	call make lr0
$	call make main
$	call make mkpar
$	call make output
$	call make reader
$	call make yaccpar
$	call make symtab
$	call make verbose
$	call make warshall
$!
$	link /exec='target/map/cross main.obj, vms_link/opt
$	goto build_last
$!
$ install :
$	WRITE SYS$ERROR "** no rule for install"
$	goto build_last
$!
$ clobber :
$	if f$search("BYACC.com") .nes. "" then delete BYACC.com;*
$	if f$search("*.exe") .nes. "" then delete *.exe;*
$! fallthru
$!
$ clean :
$	if f$search("*.obj") .nes. "" then delete *.obj;*
$	if f$search("*.bak") .nes. "" then delete *.bak;*
$	if f$search("*.lis") .nes. "" then delete *.lis;*
$	if f$search("*.log") .nes. "" then delete *.log;*
$	if f$search("*.map") .nes. "" then delete *.map;*
$	if f$search("*.opt") .nes. "" then delete *.opt;*
$! fallthru
$!
$ build_last :
$	if f$search("*.dia") .nes. "" then delete *.dia;*
$	if f$search("*.lis") .nes. "" then purge *.lis
$	if f$search("*.obj") .nes. "" then purge *.obj
$	if f$search("*.map") .nes. "" then purge *.map
$	if f$search("*.opt") .nes. "" then purge *.opt
$	if f$search("*.exe") .nes. "" then purge *.exe
$	if f$search("*.log") .nes. "" then purge *.log
$! fallthru
$!
$ vms_link_opt :
$	exit 1
$!
$! Runs BYACC from the current directory (used for testing)
$ byacc_com :
$	if "''f$search("BYACC.com")'" .nes. "" then delete BYACC.com;*
$	copy nl: BYACC.com
$	open/append  test_script BYACC.com
$	write test_script "$ temp = f$environment(""procedure"")"
$	write test_script "$ temp = temp -"
$	write test_script "		- f$parse(temp,,,""version"",""syntax_only"") -"
$	write test_script "		- f$parse(temp,,,""type"",""syntax_only"")"
$	write test_script "$ BYACC :== $ 'temp'.exe"
$	write test_script "$ define/user_mode sys$input  sys$command"
$	write test_script "$ define/user_mode sys$output sys$command"
$	write test_script "$ BYACC 'p1 'p2 'p3 'p4 'p5 'p6 'p7 'p8"
$	close test_script
$	write sys$output "** made BYACC.com"
$	exit
$!
$  else
$   mms/ignore=warning/macro=('comp','mmstar','arch') 'p3
$  endif
$ exit
$ make: subroutine
$	if f$search("''p1'.obj") .eqs. ""
$	then
$		write sys$output "compiling ''p1'"
$		'CC 'CFLAGS 'p1.c
$		if f$search("''p1'.dia") .nes. "" then delete 'p1.dia;*
$	endif
$exit
$	return
$ endsubroutine
