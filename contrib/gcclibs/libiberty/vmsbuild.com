$! libiberty/vmsbuild.com -- build liberty.olb for VMS host, VMS target
$!
$ CC	= "gcc /noVerbose/Debug/Incl=([],[-.include])"
$ LIBR	= "library /Obj"
$ LINK	= "link"
$ DELETE= "delete /noConfirm"
$ SEARCH= "search /Exact"
$ ECHO	= "write sys$output"
$ ABORT	= "exit %x002C"
$!
$ LIB_NAME = "liberty.olb"	!this is what we're going to construct
$ WORK_LIB = "new-lib.olb"	!used to guard against an incomplete build
$
$! manually copied from Makefile.in
$ REQUIRED_OFILES = "argv.o basename.o choose-temp.o concat.o cplus-dem.o "-
	+ "fdmatch.o fnmatch.o getopt.o getopt1.o getruntime.o hex.o "-
	+ "floatformat.o objalloc.o obstack.o spaces.o strerror.o strsignal.o "-
	+ "xatexit.o xexit.o xmalloc.o xmemdup.o xstrdup.o xstrerror.o"
$! anything not caught by link+search of dummy.* should be added here
$ EXTRA_OFILES = ""
$!
$! move to the directory which contains this command procedure
$ old_dir = f$environ("DEFAULT")
$ new_dir = f$parse("_._;",f$environ("PROCEDURE")) - "_._;"
$ set default 'new_dir'
$
$ ECHO "Starting libiberty build..."
$ create config.h
/* libiberty config.h for VMS */
#define NEED_sys_siglist
#define NEED_strsignal
#define NEED_psignal
#define NEED_basename
$ LIBR 'WORK_LIB' /Create
$
$! first pass: compile "required" modules
$ ofiles = REQUIRED_OFILES + " " + EXTRA_OFILES
$ pass = 1
$ gosub do_ofiles
$
$! second pass: process dummy.c, using the first pass' results
$ ECHO " now checking run-time library for missing functionality"
$ if f$search("dummy.obj").nes."" then  DELETE dummy.obj;*
$ define/noLog sys$error _NL:	!can't use /User_Mode here due to gcc
$ define/noLog sys$output _NL:	! driver's use of multiple image activation
$ on error then continue
$ 'CC' dummy.c
$ deassign sys$error   !restore, more or less
$ deassign sys$output
$ if f$search("dummy.obj").eqs."" then  goto pass2_failure1
$! link dummy.obj, capturing full linker feedback in dummy.map
$ oldmsg = f$environ("MESSAGE")
$ set message /Facility/Severity/Identification/Text
$ define/User sys$output _NL:
$ define/User sys$error _NL:
$ LINK/Map=dummy.map/noExe dummy.obj,'WORK_LIB'/Libr,-
	gnu_cc:[000000]gcclib.olb/Libr,sys$library:vaxcrtl.olb/Libr
$ set message 'oldmsg'
$ if f$search("dummy.map").eqs."" then  goto pass2_failure2
$ DELETE dummy.obj;*
$ SEARCH dummy.map "%LINK-I-UDFSYM" /Output=dummy.list
$ DELETE dummy.map;*
$ ECHO " check completed"
$! we now have a file with one entry per line of unresolvable symbols
$ ofiles = ""
$ if f$trnlnm("IFILE$").nes."" then  close/noLog ifile$
$	open/Read ifile$ dummy.list
$iloop: read/End=idone ifile$ iline
$	iline = f$edit(iline,"COMPRESS,TRIM,LOWERCASE")
$	ofiles = ofiles + " " + f$element(1," ",iline) + ".o"
$	goto iloop
$idone: close ifile$
$ DELETE dummy.list;*
$ on error then ABORT
$
$! third pass: compile "missing" modules collected in pass 2
$ pass = 3
$ gosub do_ofiles
$
$! finish up
$ LIBR 'WORK_LIB' /Compress /Output='LIB_NAME'	!new-lib.olb -> liberty.olb
$ DELETE 'WORK_LIB';*
$
$! all done
$ ECHO "Completed libiberty build."
$ type sys$input:

  You many wish to do
  $ COPY LIBERTY.OLB GNU_CC:[000000]
  so that this run-time library resides in the same location as gcc's
  support library.  When building gas, be sure to leave the original
  copy of liberty.olb here so that gas's build procedure can find it.

$ set default 'old_dir'
$ exit
$
$!
$! compile each element of the space-delimited list 'ofiles'
$!
$do_ofiles:
$ ofiles = f$edit(ofiles,"COMPRESS,TRIM")
$ i = 0
$oloop:
$ f = f$element(i," ",ofiles)
$ if f.eqs." " then  goto odone
$ f = f - ".o"	!strip dummy suffix
$ ECHO "  ''f'"
$ skip_f = 0
$ if pass.eq.3 .and. f$search("''f'.c").eqs."" then  gosub chk_deffunc
$ if .not.skip_f
$ then
$   'CC' 'f'.c
$   LIBR 'WORK_LIB' 'f'.obj /Insert
$   DELETE 'f'.obj;*
$ endif
$ i = i + 1
$ goto oloop
$odone:
$ return
$
$!
$! check functions.def for a DEFFUNC() entry corresponding to missing file 'f'.c
$!
$chk_deffunc:
$ define/User sys$output _NL:
$ define/User sys$error _NL:
$ SEARCH functions.def "DEFFUNC","''f'" /Match=AND
$ if (($status.and.%x7FFFFFFF) .eq. 1)
$ then
$   skip_f = 1
$   open/Append config_h config.h
$   write config_h "#define NEED_''f'"
$   close config_h
$ endif
$ return
$
$!
$pass2_failure1:
$! if we reach here, dummy.c failed to compile and we're really stuck
$ type sys$input:

  Cannot compile the library contents checker (dummy.c + functions.def),
  so cannot continue!

$! attempt the compile again, without suppressing diagnostic messages this time
$ on error then ABORT +0*f$verify(v)
$ v = f$verify(1)
$ 'CC' dummy.c
$ ABORT +0*f$verify(v)	!'f$verify(0)'
$!
$pass2_failure2:
$! should never reach here..
$ type sys$input:

  Cannot link the library contents checker (dummy.obj), so cannot continue!

$! attempt the link again, without suppressing diagnostic messages this time
$ on error then ABORT +0*f$verify(v)
$ v = f$verify(1)
$ LINK/Map=dummy.map/noExe dummy.obj,'WORK_LIB'/Libr,-
	gnu_cc:[000000]gcclib.olb/Libr,sys$library:vaxcrtl.olb/Libr
$ ABORT +0*f$verify(v)	!'f$verify(0)'
$
$! not reached
$ exit
