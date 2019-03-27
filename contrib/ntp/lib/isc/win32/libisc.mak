# Microsoft Developer Studio Generated NMAKE File, Based on libisc.dsp
!IF "$(CFG)" == ""
CFG=libisc - Win32 Debug
!MESSAGE No configuration specified. Defaulting to libisc - Win32 Debug.
!ENDIF 

!IF "$(CFG)" != "libisc - Win32 Release" && "$(CFG)" != "libisc - Win32 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libisc.mak" CFG="libisc - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libisc - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libisc - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

CPP=cl.exe
MTL=midl.exe
RSC=rc.exe
LIBXML=../../../../libxml2-2.7.3/win32/bin.msvc/libxml2.lib

!IF  "$(CFG)" == "libisc - Win32 Release"
_VC_MANIFEST_INC=0
_VC_MANIFEST_BASENAME=__VC80
!ELSE
_VC_MANIFEST_INC=1
_VC_MANIFEST_BASENAME=__VC80.Debug
!ENDIF

####################################################
# Specifying name of temporary resource file used only in incremental builds:

!if "$(_VC_MANIFEST_INC)" == "1"
_VC_MANIFEST_AUTO_RES=$(_VC_MANIFEST_BASENAME).auto.res
!else
_VC_MANIFEST_AUTO_RES=
!endif

####################################################
# _VC_MANIFEST_EMBED_EXE - command to embed manifest in EXE:

!if "$(_VC_MANIFEST_INC)" == "1"

#MT_SPECIAL_RETURN=1090650113
#MT_SPECIAL_SWITCH=-notify_resource_update
MT_SPECIAL_RETURN=0
MT_SPECIAL_SWITCH=
_VC_MANIFEST_EMBED_EXE= \
if exist $@.manifest mt.exe -manifest $@.manifest -out:$(_VC_MANIFEST_BASENAME).auto.manifest $(MT_SPECIAL_SWITCH) & \
if "%ERRORLEVEL%" == "$(MT_SPECIAL_RETURN)" \
rc /r $(_VC_MANIFEST_BASENAME).auto.rc & \
link $** /out:$@ $(LFLAGS)

!else

_VC_MANIFEST_EMBED_EXE= \
if exist $@.manifest mt.exe -manifest $@.manifest -outputresource:$@;1

!endif

####################################################
# _VC_MANIFEST_EMBED_DLL - command to embed manifest in DLL:

!if "$(_VC_MANIFEST_INC)" == "1"

#MT_SPECIAL_RETURN=1090650113
#MT_SPECIAL_SWITCH=-notify_resource_update
MT_SPECIAL_RETURN=0
MT_SPECIAL_SWITCH=
_VC_MANIFEST_EMBED_EXE= \
if exist $@.manifest mt.exe -manifest $@.manifest -out:$(_VC_MANIFEST_BASENAME).auto.manifest $(MT_SPECIAL_SWITCH) & \
if "%ERRORLEVEL%" == "$(MT_SPECIAL_RETURN)" \
rc /r $(_VC_MANIFEST_BASENAME).auto.rc & \
link $** /out:$@ $(LFLAGS)

!else

_VC_MANIFEST_EMBED_EXE= \
if exist $@.manifest mt.exe -manifest $@.manifest -outputresource:$@;2

!endif
####################################################
# _VC_MANIFEST_CLEAN - command to clean resources files generated temporarily:

!if "$(_VC_MANIFEST_INC)" == "1"

_VC_MANIFEST_CLEAN=-del $(_VC_MANIFEST_BASENAME).auto.res \
    $(_VC_MANIFEST_BASENAME).auto.rc \
    $(_VC_MANIFEST_BASENAME).auto.manifest

!else

_VC_MANIFEST_CLEAN=

!endif

!IF  "$(CFG)" == "libisc - Win32 Release"

OUTDIR=.\Release
INTDIR=.\Release

ALL : "..\..\..\Build\Release\libisc.dll"


CLEAN :
	-@erase "$(INTDIR)\app.obj"
	-@erase "$(INTDIR)\assertions.obj"
	-@erase "$(INTDIR)\backtrace.obj"
	-@erase "$(INTDIR)\backtrace-emptytbl.obj"
	-@erase "$(INTDIR)\base32.obj"
	-@erase "$(INTDIR)\base64.obj"
	-@erase "$(INTDIR)\bitstring.obj"
	-@erase "$(INTDIR)\buffer.obj"
	-@erase "$(INTDIR)\bufferlist.obj"
	-@erase "$(INTDIR)\commandline.obj"
	-@erase "$(INTDIR)\condition.obj"
	-@erase "$(INTDIR)\dir.obj"
	-@erase "$(INTDIR)\DLLMain.obj"
	-@erase "$(INTDIR)\entropy.obj"
	-@erase "$(INTDIR)\errno2result.obj"
	-@erase "$(INTDIR)\error.obj"
	-@erase "$(INTDIR)\event.obj"
	-@erase "$(INTDIR)\file.obj"
	-@erase "$(INTDIR)\fsaccess.obj"
	-@erase "$(INTDIR)\hash.obj"
	-@erase "$(INTDIR)\heap.obj"
	-@erase "$(INTDIR)\hex.obj"
	-@erase "$(INTDIR)\hmacmd5.obj"
	-@erase "$(INTDIR)\hmacsha.obj"
	-@erase "$(INTDIR)\httpd.obj"
	-@erase "$(INTDIR)\inet_aton.obj"
	-@erase "$(INTDIR)\inet_ntop.obj"
	-@erase "$(INTDIR)\inet_pton.obj"
	-@erase "$(INTDIR)\interfaceiter.obj"
	-@erase "$(INTDIR)\ipv6.obj"
	-@erase "$(INTDIR)\iterated_hash.obj"
	-@erase "$(INTDIR)\keyboard.obj"
	-@erase "$(INTDIR)\lex.obj"
	-@erase "$(INTDIR)\lfsr.obj"
	-@erase "$(INTDIR)\lib.obj"
	-@erase "$(INTDIR)\log.obj"
	-@erase "$(INTDIR)\md5.obj"
	-@erase "$(INTDIR)\mem.obj"
	-@erase "$(INTDIR)\msgcat.obj"
	-@erase "$(INTDIR)\mutexblock.obj"
	-@erase "$(INTDIR)\net.obj"
	-@erase "$(INTDIR)\netaddr.obj"
	-@erase "$(INTDIR)\netscope.obj"
	-@erase "$(INTDIR)\ntpaths.obj"
	-@erase "$(INTDIR)\once.obj"
	-@erase "$(INTDIR)\ondestroy.obj"
	-@erase "$(INTDIR)\os.obj"
	-@erase "$(INTDIR)\parseint.obj"
	-@erase "$(INTDIR)\portset.obj"
	-@erase "$(INTDIR)\quota.obj"
	-@erase "$(INTDIR)\radix.obj"
	-@erase "$(INTDIR)\random.obj"
	-@erase "$(INTDIR)\ratelimiter.obj"
	-@erase "$(INTDIR)\refcount.obj"
	-@erase "$(INTDIR)\region.obj"
	-@erase "$(INTDIR)\resource.obj"
	-@erase "$(INTDIR)\result.obj"
	-@erase "$(INTDIR)\rwlock.obj"
	-@erase "$(INTDIR)\serial.obj"
	-@erase "$(INTDIR)\sha1.obj"
	-@erase "$(INTDIR)\sha2.obj"
	-@erase "$(INTDIR)\sockaddr.obj"
	-@erase "$(INTDIR)\socket.obj"
	-@erase "$(INTDIR)\stats.obj"
	-@erase "$(INTDIR)\stdio.obj"
	-@erase "$(INTDIR)\stdtime.obj"
	-@erase "$(INTDIR)\strerror.obj"
	-@erase "$(INTDIR)\string.obj"
	-@erase "$(INTDIR)\symtab.obj"
	-@erase "$(INTDIR)\syslog.obj"
	-@erase "$(INTDIR)\task.obj"
	-@erase "$(INTDIR)\taskpool.obj"
	-@erase "$(INTDIR)\thread.obj"
	-@erase "$(INTDIR)\time.obj"
	-@erase "$(INTDIR)\timer.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(INTDIR)\version.obj"
	-@erase "$(INTDIR)\win32os.obj"
	-@erase "$(OUTDIR)\libisc.exp"
	-@erase "$(OUTDIR)\libisc.lib"
	-@erase "..\..\..\Build\Release\libisc.dll"
	-@$(_VC_MANIFEST_CLEAN)

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP_PROJ=/nologo /MD /W3 /GX /O2 /I "./" /I "../../../" /I "include" /I "../include" /I "../../../lib/isc/noatomic/include" /I "win32" /I "../../isccfg/include" /I "../../../../libxml2-2.7.3/include" /D "BIND9" /D "WIN32" /D "NDEBUG" /D "__STDC__" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBISC_EXPORTS" /Fp"$(INTDIR)\libisc.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /c 
MTL_PROJ=/nologo /D "NDEBUG" /mktyplib203 /win32 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\libisc.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=user32.lib advapi32.lib ws2_32.lib $(LIBXML) /nologo /dll /incremental:no /pdb:"$(OUTDIR)\libisc.pdb" /machine:I386 /def:".\libisc.def" /out:"../../../Build/Release/libisc.dll" /implib:"$(OUTDIR)\libisc.lib" 
DEF_FILE= \
	".\libisc.def"
LINK32_OBJS= \
	"$(INTDIR)\app.obj" \
	"$(INTDIR)\condition.obj" \
	"$(INTDIR)\dir.obj" \
	"$(INTDIR)\DLLMain.obj" \
	"$(INTDIR)\entropy.obj" \
	"$(INTDIR)\errno2result.obj" \
	"$(INTDIR)\file.obj" \
	"$(INTDIR)\fsaccess.obj" \
	"$(INTDIR)\interfaceiter.obj" \
	"$(INTDIR)\ipv6.obj" \
	"$(INTDIR)\iterated_hash.obj" \
	"$(INTDIR)\keyboard.obj" \
	"$(INTDIR)\net.obj" \
	"$(INTDIR)\ntpaths.obj" \
	"$(INTDIR)\once.obj" \
	"$(INTDIR)\os.obj" \
	"$(INTDIR)\resource.obj" \
	"$(INTDIR)\socket.obj" \
	"$(INTDIR)\stdio.obj" \
	"$(INTDIR)\stdtime.obj" \
	"$(INTDIR)\strerror.obj" \
	"$(INTDIR)\syslog.obj" \
	"$(INTDIR)\thread.obj" \
	"$(INTDIR)\time.obj" \
	"$(INTDIR)\version.obj" \
	"$(INTDIR)\win32os.obj" \
	"$(INTDIR)\assertions.obj" \
	"$(INTDIR)\backtrace.obj" \
	"$(INTDIR)\backtrace-emptytbl.obj" \
	"$(INTDIR)\base32.obj" \
	"$(INTDIR)\base64.obj" \
	"$(INTDIR)\bitstring.obj" \
	"$(INTDIR)\buffer.obj" \
	"$(INTDIR)\bufferlist.obj" \
	"$(INTDIR)\commandline.obj" \
	"$(INTDIR)\error.obj" \
	"$(INTDIR)\event.obj" \
	"$(INTDIR)\hash.obj" \
	"$(INTDIR)\heap.obj" \
	"$(INTDIR)\hex.obj" \
	"$(INTDIR)\hmacmd5.obj" \
	"$(INTDIR)\hmacsha.obj" \
	"$(INTDIR)\httpd.obj" \
	"$(INTDIR)\inet_aton.obj" \
	"$(INTDIR)\inet_ntop.obj" \
	"$(INTDIR)\inet_pton.obj" \
	"$(INTDIR)\lex.obj" \
	"$(INTDIR)\lfsr.obj" \
	"$(INTDIR)\lib.obj" \
	"$(INTDIR)\log.obj" \
	"$(INTDIR)\md5.obj" \
	"$(INTDIR)\mem.obj" \
	"$(INTDIR)\msgcat.obj" \
	"$(INTDIR)\mutexblock.obj" \
	"$(INTDIR)\netaddr.obj" \
	"$(INTDIR)\netscope.obj" \
	"$(INTDIR)\ondestroy.obj" \
	"$(INTDIR)\quota.obj" \
	"$(INTDIR)\radix.obj" \
	"$(INTDIR)\random.obj" \
	"$(INTDIR)\ratelimiter.obj" \
	"$(INTDIR)\refcount.obj" \
	"$(INTDIR)\result.obj" \
	"$(INTDIR)\rwlock.obj" \
	"$(INTDIR)\serial.obj" \
	"$(INTDIR)\sha1.obj" \
	"$(INTDIR)\sha2.obj" \
	"$(INTDIR)\sockaddr.obj" \
	"$(INTDIR)\stats.obj" \
	"$(INTDIR)\string.obj" \
	"$(INTDIR)\symtab.obj" \
	"$(INTDIR)\task.obj" \
	"$(INTDIR)\taskpool.obj" \
	"$(INTDIR)\timer.obj" \
	"$(INTDIR)\parseint.obj" \
	"$(INTDIR)\portset.obj" \
	"$(INTDIR)\region.obj"

"..\..\..\Build\Release\libisc.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<
  $(_VC_MANIFEST_EMBED_DLL)

!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"

OUTDIR=.\Debug
INTDIR=.\Debug
# Begin Custom Macros
OutDir=.\Debug
# End Custom Macros

ALL : "..\..\..\Build\Debug\libisc.dll" "$(OUTDIR)\libisc.bsc"


CLEAN :
	-@erase "$(INTDIR)\app.obj"
	-@erase "$(INTDIR)\app.sbr"
	-@erase "$(INTDIR)\assertions.obj"
	-@erase "$(INTDIR)\assertions.sbr"
	-@erase "$(INTDIR)\backtrace.obj"
	-@erase "$(INTDIR)\backtrace-emptytbl.obj"
	-@erase "$(INTDIR)\backtrace.sbr"
	-@erase "$(INTDIR)\backtrace-emptytbl.sbr"
	-@erase "$(INTDIR)\base32.obj"
	-@erase "$(INTDIR)\base32.sbr"
	-@erase "$(INTDIR)\base64.obj"
	-@erase "$(INTDIR)\base64.sbr"
	-@erase "$(INTDIR)\bitstring.obj"
	-@erase "$(INTDIR)\bitstring.sbr"
	-@erase "$(INTDIR)\buffer.obj"
	-@erase "$(INTDIR)\buffer.sbr"
	-@erase "$(INTDIR)\bufferlist.obj"
	-@erase "$(INTDIR)\bufferlist.sbr"
	-@erase "$(INTDIR)\commandline.obj"
	-@erase "$(INTDIR)\commandline.sbr"
	-@erase "$(INTDIR)\condition.obj"
	-@erase "$(INTDIR)\condition.sbr"
	-@erase "$(INTDIR)\dir.obj"
	-@erase "$(INTDIR)\dir.sbr"
	-@erase "$(INTDIR)\DLLMain.obj"
	-@erase "$(INTDIR)\DLLMain.sbr"
	-@erase "$(INTDIR)\entropy.obj"
	-@erase "$(INTDIR)\entropy.sbr"
	-@erase "$(INTDIR)\errno2result.obj"
	-@erase "$(INTDIR)\errno2result.sbr"
	-@erase "$(INTDIR)\error.obj"
	-@erase "$(INTDIR)\error.sbr"
	-@erase "$(INTDIR)\event.obj"
	-@erase "$(INTDIR)\event.sbr"
	-@erase "$(INTDIR)\file.obj"
	-@erase "$(INTDIR)\file.sbr"
	-@erase "$(INTDIR)\fsaccess.obj"
	-@erase "$(INTDIR)\fsaccess.sbr"
	-@erase "$(INTDIR)\hash.obj"
	-@erase "$(INTDIR)\hash.sbr"
	-@erase "$(INTDIR)\heap.obj"
	-@erase "$(INTDIR)\heap.sbr"
	-@erase "$(INTDIR)\hex.obj"
	-@erase "$(INTDIR)\hex.sbr"
	-@erase "$(INTDIR)\hmacmd5.obj"
	-@erase "$(INTDIR)\hmacmd5.sbr"
	-@erase "$(INTDIR)\hmacsha.obj"
	-@erase "$(INTDIR)\hmacsha.sbr"
	-@erase "$(INTDIR)\httpd.obj"
	-@erase "$(INTDIR)\httpd.sbr"
	-@erase "$(INTDIR)\inet_aton.obj"
	-@erase "$(INTDIR)\inet_aton.sbr"
	-@erase "$(INTDIR)\inet_ntop.obj"
	-@erase "$(INTDIR)\inet_ntop.sbr"
	-@erase "$(INTDIR)\inet_pton.obj"
	-@erase "$(INTDIR)\inet_pton.sbr"
	-@erase "$(INTDIR)\interfaceiter.obj"
	-@erase "$(INTDIR)\interfaceiter.sbr"
	-@erase "$(INTDIR)\ipv6.obj"
	-@erase "$(INTDIR)\ipv6.sbr"
	-@erase "$(INTDIR)\iterated_hash.obj"
	-@erase "$(INTDIR)\iterated_hash.sbr"
	-@erase "$(INTDIR)\keyboard.obj"
	-@erase "$(INTDIR)\keyboard.sbr"
	-@erase "$(INTDIR)\lex.obj"
	-@erase "$(INTDIR)\lex.sbr"
	-@erase "$(INTDIR)\lfsr.obj"
	-@erase "$(INTDIR)\lfsr.sbr"
	-@erase "$(INTDIR)\lib.obj"
	-@erase "$(INTDIR)\lib.sbr"
	-@erase "$(INTDIR)\log.obj"
	-@erase "$(INTDIR)\log.sbr"
	-@erase "$(INTDIR)\md5.obj"
	-@erase "$(INTDIR)\md5.sbr"
	-@erase "$(INTDIR)\mem.obj"
	-@erase "$(INTDIR)\mem.sbr"
	-@erase "$(INTDIR)\msgcat.obj"
	-@erase "$(INTDIR)\msgcat.sbr"
	-@erase "$(INTDIR)\mutexblock.obj"
	-@erase "$(INTDIR)\mutexblock.sbr"
	-@erase "$(INTDIR)\net.obj"
	-@erase "$(INTDIR)\net.sbr"
	-@erase "$(INTDIR)\netaddr.obj"
	-@erase "$(INTDIR)\netaddr.sbr"
	-@erase "$(INTDIR)\netscope.obj"
	-@erase "$(INTDIR)\netscope.sbr"
	-@erase "$(INTDIR)\ntpaths.obj"
	-@erase "$(INTDIR)\ntpaths.sbr"
	-@erase "$(INTDIR)\once.obj"
	-@erase "$(INTDIR)\once.sbr"
	-@erase "$(INTDIR)\ondestroy.obj"
	-@erase "$(INTDIR)\ondestroy.sbr"
	-@erase "$(INTDIR)\os.obj"
	-@erase "$(INTDIR)\os.sbr"
	-@erase "$(INTDIR)\parseint.obj"
	-@erase "$(INTDIR)\parseint.sbr"
	-@erase "$(INTDIR)\portset.obj"
	-@erase "$(INTDIR)\portset.sbr"
	-@erase "$(INTDIR)\quota.obj"
	-@erase "$(INTDIR)\quota.sbr"
	-@erase "$(INTDIR)\radix.obj"
	-@erase "$(INTDIR)\radix.sbr"
	-@erase "$(INTDIR)\random.obj"
	-@erase "$(INTDIR)\random.sbr"
	-@erase "$(INTDIR)\ratelimiter.obj"
	-@erase "$(INTDIR)\ratelimiter.sbr"
	-@erase "$(INTDIR)\refcount.obj"
	-@erase "$(INTDIR)\refcount.sbr"
	-@erase "$(INTDIR)\region.obj"
	-@erase "$(INTDIR)\region.sbr"
	-@erase "$(INTDIR)\resource.obj"
	-@erase "$(INTDIR)\resource.sbr"
	-@erase "$(INTDIR)\result.obj"
	-@erase "$(INTDIR)\result.sbr"
	-@erase "$(INTDIR)\rwlock.obj"
	-@erase "$(INTDIR)\rwlock.sbr"
	-@erase "$(INTDIR)\serial.obj"
	-@erase "$(INTDIR)\serial.sbr"
	-@erase "$(INTDIR)\sha1.obj"
	-@erase "$(INTDIR)\sha1.sbr"
	-@erase "$(INTDIR)\sha2.obj"
	-@erase "$(INTDIR)\sha2.sbr"
	-@erase "$(INTDIR)\sockaddr.obj"
	-@erase "$(INTDIR)\sockaddr.sbr"
	-@erase "$(INTDIR)\socket.obj"
	-@erase "$(INTDIR)\socket.sbr"
	-@erase "$(INTDIR)\stats.obj"
	-@erase "$(INTDIR)\stats.sbr"
	-@erase "$(INTDIR)\stdio.obj"
	-@erase "$(INTDIR)\stdio.sbr"
	-@erase "$(INTDIR)\stdtime.obj"
	-@erase "$(INTDIR)\stdtime.sbr"
	-@erase "$(INTDIR)\strerror.obj"
	-@erase "$(INTDIR)\strerror.sbr"
	-@erase "$(INTDIR)\string.obj"
	-@erase "$(INTDIR)\string.sbr"
	-@erase "$(INTDIR)\symtab.obj"
	-@erase "$(INTDIR)\symtab.sbr"
	-@erase "$(INTDIR)\syslog.obj"
	-@erase "$(INTDIR)\syslog.sbr"
	-@erase "$(INTDIR)\task.obj"
	-@erase "$(INTDIR)\task.sbr"
	-@erase "$(INTDIR)\taskpool.obj"
	-@erase "$(INTDIR)\taskpool.sbr"
	-@erase "$(INTDIR)\thread.obj"
	-@erase "$(INTDIR)\thread.sbr"
	-@erase "$(INTDIR)\time.obj"
	-@erase "$(INTDIR)\time.sbr"
	-@erase "$(INTDIR)\timer.obj"
	-@erase "$(INTDIR)\timer.sbr"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(INTDIR)\vc60.pdb"
	-@erase "$(INTDIR)\version.obj"
	-@erase "$(INTDIR)\version.sbr"
	-@erase "$(INTDIR)\win32os.obj"
	-@erase "$(INTDIR)\win32os.sbr"
	-@erase "$(OUTDIR)\libisc.bsc"
	-@erase "$(OUTDIR)\libisc.exp"
	-@erase "$(OUTDIR)\libisc.lib"
	-@erase "$(OUTDIR)\libisc.map"
	-@erase "$(OUTDIR)\libisc.pdb"
	-@erase "..\..\..\Build\Debug\libisc.dll"
	-@erase "..\..\..\Build\Debug\libisc.ilk"
	-@$(_VC_MANIFEST_CLEAN)

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP_PROJ=/nologo /MDd /W3 /Gm /GX /ZI /Od /I "./" /I "../../../" /I "include" /I "../include" /I "../../../lib/isc/noatomic/include" /I "win32" /I "../../isccfg/include" /I "../../../../libxml2-2.7.3/include" /D "BIND9" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "__STDC__" /D "_MBCS" /D "_USRDLL" /D "LIBISC_EXPORTS" /FR"$(INTDIR)\\" /Fp"$(INTDIR)\libisc.pch" /YX /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /GZ /c 
MTL_PROJ=/nologo /D "_DEBUG" /mktyplib203 /win32 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\libisc.bsc" 
BSC32_SBRS= \
	"$(INTDIR)\app.sbr" \
	"$(INTDIR)\condition.sbr" \
	"$(INTDIR)\dir.sbr" \
	"$(INTDIR)\DLLMain.sbr" \
	"$(INTDIR)\entropy.sbr" \
	"$(INTDIR)\errno2result.sbr" \
	"$(INTDIR)\file.sbr" \
	"$(INTDIR)\fsaccess.sbr" \
	"$(INTDIR)\interfaceiter.sbr" \
	"$(INTDIR)\ipv6.sbr" \
	"$(INTDIR)\iterated_hash.sbr" \
	"$(INTDIR)\keyboard.sbr" \
	"$(INTDIR)\net.sbr" \
	"$(INTDIR)\ntpaths.sbr" \
	"$(INTDIR)\once.sbr" \
	"$(INTDIR)\os.sbr" \
	"$(INTDIR)\resource.sbr" \
	"$(INTDIR)\socket.sbr" \
	"$(INTDIR)\stdio.sbr" \
	"$(INTDIR)\stdtime.sbr" \
	"$(INTDIR)\strerror.sbr" \
	"$(INTDIR)\syslog.sbr" \
	"$(INTDIR)\thread.sbr" \
	"$(INTDIR)\time.sbr" \
	"$(INTDIR)\version.sbr" \
	"$(INTDIR)\win32os.sbr" \
	"$(INTDIR)\assertions.sbr" \
	"$(INTDIR)\backtrace.sbr" \
	"$(INTDIR)\backtrace-emptytbl.sbr" \
	"$(INTDIR)\base32.sbr" \
	"$(INTDIR)\base64.sbr" \
	"$(INTDIR)\bitstring.sbr" \
	"$(INTDIR)\buffer.sbr" \
	"$(INTDIR)\bufferlist.sbr" \
	"$(INTDIR)\commandline.sbr" \
	"$(INTDIR)\error.sbr" \
	"$(INTDIR)\event.sbr" \
	"$(INTDIR)\hash.sbr" \
	"$(INTDIR)\heap.sbr" \
	"$(INTDIR)\hex.sbr" \
	"$(INTDIR)\hmacmd5.sbr" \
	"$(INTDIR)\hmacsha.sbr" \
	"$(INTDIR)\httpd.sbr" \
	"$(INTDIR)\inet_aton.sbr" \
	"$(INTDIR)\inet_ntop.sbr" \
	"$(INTDIR)\inet_pton.sbr" \
	"$(INTDIR)\lex.sbr" \
	"$(INTDIR)\lfsr.sbr" \
	"$(INTDIR)\lib.sbr" \
	"$(INTDIR)\log.sbr" \
	"$(INTDIR)\md5.sbr" \
	"$(INTDIR)\mem.sbr" \
	"$(INTDIR)\msgcat.sbr" \
	"$(INTDIR)\mutexblock.sbr" \
	"$(INTDIR)\netaddr.sbr" \
	"$(INTDIR)\netscope.sbr" \
	"$(INTDIR)\ondestroy.sbr" \
	"$(INTDIR)\quota.sbr" \
	"$(INTDIR)\radix.sbr" \
	"$(INTDIR)\random.sbr" \
	"$(INTDIR)\ratelimiter.sbr" \
	"$(INTDIR)\refcount.sbr" \
	"$(INTDIR)\result.sbr" \
	"$(INTDIR)\rwlock.sbr" \
	"$(INTDIR)\serial.sbr" \
	"$(INTDIR)\sha1.sbr" \
	"$(INTDIR)\sha2.sbr" \
	"$(INTDIR)\sockaddr.sbr" \
	"$(INTDIR)\stats.sbr" \
	"$(INTDIR)\string.sbr" \
	"$(INTDIR)\symtab.sbr" \
	"$(INTDIR)\task.sbr" \
	"$(INTDIR)\taskpool.sbr" \
	"$(INTDIR)\timer.sbr" \
	"$(INTDIR)\parseint.sbr" \
	"$(INTDIR)\portset.sbr" \
	"$(INTDIR)\region.sbr"

"$(OUTDIR)\libisc.bsc" : "$(OUTDIR)" $(BSC32_SBRS)
    $(BSC32) @<<
  $(BSC32_FLAGS) $(BSC32_SBRS)
<<

LINK32=link.exe
LINK32_FLAGS=user32.lib advapi32.lib ws2_32.lib $(LIBXML) /nologo /dll /incremental:yes /pdb:"$(OUTDIR)\libisc.pdb" /map:"$(INTDIR)\libisc.map" /debug /machine:I386 /def:".\libisc.def" /out:"../../../Build/Debug/libisc.dll" /implib:"$(OUTDIR)\libisc.lib" /pdbtype:sept 
DEF_FILE= \
	".\libisc.def"
LINK32_OBJS= \
	"$(INTDIR)\app.obj" \
	"$(INTDIR)\condition.obj" \
	"$(INTDIR)\dir.obj" \
	"$(INTDIR)\DLLMain.obj" \
	"$(INTDIR)\entropy.obj" \
	"$(INTDIR)\errno2result.obj" \
	"$(INTDIR)\file.obj" \
	"$(INTDIR)\fsaccess.obj" \
	"$(INTDIR)\interfaceiter.obj" \
	"$(INTDIR)\ipv6.obj" \
	"$(INTDIR)\iterated_hash.obj" \
	"$(INTDIR)\keyboard.obj" \
	"$(INTDIR)\net.obj" \
	"$(INTDIR)\ntpaths.obj" \
	"$(INTDIR)\once.obj" \
	"$(INTDIR)\os.obj" \
	"$(INTDIR)\resource.obj" \
	"$(INTDIR)\socket.obj" \
	"$(INTDIR)\stdio.obj" \
	"$(INTDIR)\stdtime.obj" \
	"$(INTDIR)\strerror.obj" \
	"$(INTDIR)\syslog.obj" \
	"$(INTDIR)\thread.obj" \
	"$(INTDIR)\time.obj" \
	"$(INTDIR)\version.obj" \
	"$(INTDIR)\win32os.obj" \
	"$(INTDIR)\assertions.obj" \
	"$(INTDIR)\backtrace.obj" \
	"$(INTDIR)\backtrace-emptytbl.obj" \
	"$(INTDIR)\base32.obj" \
	"$(INTDIR)\base64.obj" \
	"$(INTDIR)\bitstring.obj" \
	"$(INTDIR)\buffer.obj" \
	"$(INTDIR)\bufferlist.obj" \
	"$(INTDIR)\commandline.obj" \
	"$(INTDIR)\error.obj" \
	"$(INTDIR)\event.obj" \
	"$(INTDIR)\hash.obj" \
	"$(INTDIR)\heap.obj" \
	"$(INTDIR)\hex.obj" \
	"$(INTDIR)\hmacmd5.obj" \
	"$(INTDIR)\hmacsha.obj" \
	"$(INTDIR)\httpd.obj" \
	"$(INTDIR)\inet_aton.obj" \
	"$(INTDIR)\inet_ntop.obj" \
	"$(INTDIR)\inet_pton.obj" \
	"$(INTDIR)\lex.obj" \
	"$(INTDIR)\lfsr.obj" \
	"$(INTDIR)\lib.obj" \
	"$(INTDIR)\log.obj" \
	"$(INTDIR)\md5.obj" \
	"$(INTDIR)\mem.obj" \
	"$(INTDIR)\msgcat.obj" \
	"$(INTDIR)\mutexblock.obj" \
	"$(INTDIR)\netaddr.obj" \
	"$(INTDIR)\netscope.obj" \
	"$(INTDIR)\ondestroy.obj" \
	"$(INTDIR)\quota.obj" \
	"$(INTDIR)\radix.obj" \
	"$(INTDIR)\random.obj" \
	"$(INTDIR)\ratelimiter.obj" \
	"$(INTDIR)\refcount.obj" \
	"$(INTDIR)\result.obj" \
	"$(INTDIR)\rwlock.obj" \
	"$(INTDIR)\serial.obj" \
	"$(INTDIR)\sha1.obj" \
	"$(INTDIR)\sha2.obj" \
	"$(INTDIR)\sockaddr.obj" \
	"$(INTDIR)\stats.obj" \
	"$(INTDIR)\string.obj" \
	"$(INTDIR)\symtab.obj" \
	"$(INTDIR)\task.obj" \
	"$(INTDIR)\taskpool.obj" \
	"$(INTDIR)\timer.obj" \
	"$(INTDIR)\parseint.obj" \
	"$(INTDIR)\portset.obj" \
	"$(INTDIR)\region.obj"

"..\..\..\Build\Debug\libisc.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<
  $(_VC_MANIFEST_EMBED_DLL)

!ENDIF 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<


!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("libisc.dep")
!INCLUDE "libisc.dep"
!ELSE 
!MESSAGE Warning: cannot find "libisc.dep"
!ENDIF 
!ENDIF 


!IF "$(CFG)" == "libisc - Win32 Release" || "$(CFG)" == "libisc - Win32 Debug"
SOURCE=.\app.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\app.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\app.obj"	"$(INTDIR)\app.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\condition.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\condition.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\condition.obj"	"$(INTDIR)\condition.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\dir.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\dir.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\dir.obj"	"$(INTDIR)\dir.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\DLLMain.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\DLLMain.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\DLLMain.obj"	"$(INTDIR)\DLLMain.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\entropy.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\entropy.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\entropy.obj"	"$(INTDIR)\entropy.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\errno2result.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\errno2result.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\errno2result.obj"	"$(INTDIR)\errno2result.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\file.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\file.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\file.obj"	"$(INTDIR)\file.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\fsaccess.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\fsaccess.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\fsaccess.obj"	"$(INTDIR)\fsaccess.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\interfaceiter.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\interfaceiter.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\interfaceiter.obj"	"$(INTDIR)\interfaceiter.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\ipv6.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\ipv6.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\ipv6.obj"	"$(INTDIR)\ipv6.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 


SOURCE=.\keyboard.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\keyboard.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\keyboard.obj"	"$(INTDIR)\keyboard.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\net.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\net.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\net.obj"	"$(INTDIR)\net.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\ntpaths.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\ntpaths.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\ntpaths.obj"	"$(INTDIR)\ntpaths.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\once.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\once.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\once.obj"	"$(INTDIR)\once.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\os.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\os.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\os.obj"	"$(INTDIR)\os.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\resource.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\resource.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\resource.obj"	"$(INTDIR)\resource.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\socket.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\socket.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\socket.obj"	"$(INTDIR)\socket.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\stdio.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\stdio.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\stdio.obj"	"$(INTDIR)\stdio.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\stdtime.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\stdtime.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\stdtime.obj"	"$(INTDIR)\stdtime.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\strerror.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\strerror.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\strerror.obj"	"$(INTDIR)\strerror.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\syslog.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\syslog.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\syslog.obj"	"$(INTDIR)\syslog.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\thread.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\thread.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\thread.obj"	"$(INTDIR)\thread.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\time.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\time.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\time.obj"	"$(INTDIR)\time.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\version.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\version.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\version.obj"	"$(INTDIR)\version.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=.\win32os.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\win32os.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\win32os.obj"	"$(INTDIR)\win32os.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 

SOURCE=..\assertions.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\assertions.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\assertions.obj"	"$(INTDIR)\assertions.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\backtrace.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\backtrace.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\backtrace.obj"	"$(INTDIR)\backtrace.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\backtrace-emptytbl.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\backtrace-emptytbl.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\backtrace-emptytbl.obj"	"$(INTDIR)\backtrace-emptytbl.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\base32.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\base32.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\base32.obj"	"$(INTDIR)\base32.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\base64.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\base64.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\base64.obj"	"$(INTDIR)\base64.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\bitstring.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\bitstring.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\bitstring.obj"	"$(INTDIR)\bitstring.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\buffer.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\buffer.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\buffer.obj"	"$(INTDIR)\buffer.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\bufferlist.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\bufferlist.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\bufferlist.obj"	"$(INTDIR)\bufferlist.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\commandline.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\commandline.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\commandline.obj"	"$(INTDIR)\commandline.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\error.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\error.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\error.obj"	"$(INTDIR)\error.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\event.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\event.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\event.obj"	"$(INTDIR)\event.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\hash.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\hash.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\hash.obj"	"$(INTDIR)\hash.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\heap.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\heap.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\heap.obj"	"$(INTDIR)\heap.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\hex.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\hex.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\hex.obj"	"$(INTDIR)\hex.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\hmacmd5.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\hmacmd5.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\hmacmd5.obj"	"$(INTDIR)\hmacmd5.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\hmacsha.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\hmacsha.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\hmacsha.obj"	"$(INTDIR)\hmacsha.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\httpd.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\httpd.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\httpd.obj"	"$(INTDIR)\httpd.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\inet_aton.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\inet_aton.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\inet_aton.obj"	"$(INTDIR)\inet_aton.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\inet_ntop.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\inet_ntop.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\inet_ntop.obj"	"$(INTDIR)\inet_ntop.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\inet_pton.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\inet_pton.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\inet_pton.obj"	"$(INTDIR)\inet_pton.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\iterated_hash.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\iterated_hash.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\iterated_hash.obj"	"$(INTDIR)\iterated_hash.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\lex.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\lex.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\lex.obj"	"$(INTDIR)\lex.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\lfsr.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\lfsr.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\lfsr.obj"	"$(INTDIR)\lfsr.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\lib.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\lib.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\lib.obj"	"$(INTDIR)\lib.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\log.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\log.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\log.obj"	"$(INTDIR)\log.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\md5.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\md5.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\md5.obj"	"$(INTDIR)\md5.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\mem.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\mem.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\mem.obj"	"$(INTDIR)\mem.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\nls\msgcat.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\msgcat.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\msgcat.obj"	"$(INTDIR)\msgcat.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\mutexblock.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\mutexblock.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\mutexblock.obj"	"$(INTDIR)\mutexblock.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\netaddr.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\netaddr.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\netaddr.obj"	"$(INTDIR)\netaddr.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\netscope.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\netscope.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\netscope.obj"	"$(INTDIR)\netscope.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\ondestroy.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\ondestroy.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\ondestroy.obj"	"$(INTDIR)\ondestroy.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\parseint.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\parseint.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\parseint.obj"	"$(INTDIR)\parseint.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\portset.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\portset.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\portset.obj"	"$(INTDIR)\portset.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\quota.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\quota.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\quota.obj"	"$(INTDIR)\quota.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\radix.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\radix.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\radix.obj"	"$(INTDIR)\radix.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\random.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\random.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\random.obj"	"$(INTDIR)\random.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\ratelimiter.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\ratelimiter.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\ratelimiter.obj"	"$(INTDIR)\ratelimiter.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\refcount.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\refcount.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\refcount.obj"	"$(INTDIR)\refcount.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\region.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\region.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\region.obj"	"$(INTDIR)\region.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\result.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\result.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\result.obj"	"$(INTDIR)\result.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\rwlock.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\rwlock.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\rwlock.obj"	"$(INTDIR)\rwlock.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\serial.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\serial.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\serial.obj"	"$(INTDIR)\serial.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\sha1.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\sha1.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\sha1.obj"	"$(INTDIR)\sha1.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\sha2.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\sha2.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\sha2.obj"	"$(INTDIR)\sha2.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\sockaddr.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\sockaddr.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\sockaddr.obj"	"$(INTDIR)\sockaddr.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\stats.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\stats.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\stats.obj"	"$(INTDIR)\stats.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\string.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\string.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\string.obj"	"$(INTDIR)\string.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\symtab.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\symtab.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\symtab.obj"	"$(INTDIR)\symtab.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\task.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\task.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\task.obj"	"$(INTDIR)\task.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\taskpool.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\taskpool.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\taskpool.obj"	"$(INTDIR)\taskpool.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 

SOURCE=..\timer.c

!IF  "$(CFG)" == "libisc - Win32 Release"


"$(INTDIR)\timer.obj" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"


"$(INTDIR)\timer.obj"	"$(INTDIR)\timer.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


!ENDIF 


!ENDIF 

####################################################
# Commands to generate initial empty manifest file and the RC file
# that references it, and for generating the .res file:

$(_VC_MANIFEST_BASENAME).auto.res : $(_VC_MANIFEST_BASENAME).auto.rc

$(_VC_MANIFEST_BASENAME).auto.rc : $(_VC_MANIFEST_BASENAME).auto.manifest
    type <<$@
#include <winuser.h>
1RT_MANIFEST"$(_VC_MANIFEST_BASENAME).auto.manifest"
<< KEEP

$(_VC_MANIFEST_BASENAME).auto.manifest :
    type <<$@
<?xml version='1.0' encoding='UTF-8' standalone='yes'?>
<assembly xmlns='urn:schemas-microsoft-com:asm.v1' manifestVersion='1.0'>
</assembly>
<< KEEP
