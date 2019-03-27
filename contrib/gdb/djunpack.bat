@echo off
Rem
Rem WARNING WARNING WARNING: This file needs to have DOS CRLF end-of-line
Rem format, or else stock DOS/Windows shells will refuse to run it.
Rem
Rem This batch file unpacks the GDB distribution while simultaneously
Rem renaming some of the files whose names are invalid on DOS or conflict
Rem with other file names after truncation to DOS 8+3 namespace.
Rem
Rem Invoke like this:
Rem
Rem     djunpack gdb-XYZ.tar
Rem
Rem where XYZ is the version number.  If the argument includes leading
Rem directories, it MUST use backslashes, not forward slashes.
Rem
Rem The following 2 lines need to be changed with each new GDB release, to
Rem be identical to the name of the top-level directory where the GDB
Rem distribution unpacks itself.
set GDBVER=gdb-6.1.1
if "%GDBVER%"=="gdb-6.1.1" GoTo EnvOk
Rem If their environment space is too small, re-exec with a larger one
command.com /e:4096 /c %0 %1
GoTo End
:EnvOk
if not exist %1 GoTo NoArchive
djtar -x -p -o %GDBVER%/gdb/config/djgpp/fnchange.lst %1 > fnchange.tmp
Rem The following uses a feature of COPY whereby it does not copy
Rem empty files.  We need that because the previous line will create
Rem an empty fnchange.tmp even if the command failed for some reason.
copy fnchange.tmp junk.tmp > nul
if not exist junk.tmp GoTo NoDjTar
del junk.tmp
sed -e 's,@V@,%GDBVER%,g' < fnchange.tmp > fnchange.lst
Rem See the comment above about the reason for using COPY.
copy fnchange.lst junk.tmp > nul
if not exist junk.tmp GoTo NoSed
del junk.tmp
djtar -x -n fnchange.lst %1
GoTo End
:NoSed
echo FAIL: Sed is not available.
GoTo End
:NoDjTar
echo FAIL: DJTAR is not available or no fnchange.lst file in %1.
GoTo End
:NoArchive
echo FAIL: the file %1 does not seem to exist.
echo Remember that %1 cannot use forward slashes, only backslashes.
GoTo End
:End
set GDBVER=
