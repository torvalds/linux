@echo off
rem --------------------------------------------------------------
rem -- DNS cache save/load script
rem --
rem -- Version 1.2
rem -- By Yuri Voinov (c) 2014
rem --------------------------------------------------------------

rem Variables
set prefix="C:\Program Files (x86)"
set program_path=%prefix%\Unbound
set uc=%program_path%\unbound-control.exe
set fname="unbound_cache.dmp"

rem Check Unbound installed
if exist %uc% goto start
echo Unbound control not found. Exiting...
exit 1

:start

rem arg1 - command (optional)
rem arg2 - file name (optional)
set arg1=%1
set arg2=%2

if /I "%arg1%" == "-h" goto help

if "%arg1%" == "" (
echo Loading cache from %program_path%\%fname%
dir /a %program_path%\%fname%
type %program_path%\%fname%|%uc% load_cache
goto end
)

if defined %arg2% (goto Not_Defined) else (goto Defined)

rem If file not specified; use default dump file
:Not_defined
if /I "%arg1%" == "-s" (
echo Saving cache to %program_path%\%fname%
%uc% dump_cache>%program_path%\%fname%
dir /a %program_path%\%fname%
echo ok
goto end
)

if /I "%arg1%" == "-l" (
echo Loading cache from %program_path%\%fname%
dir /a %program_path%\%fname%
type %program_path%\%fname%|%uc% load_cache
goto end
)

if /I "%arg1%" == "-r" (
echo Saving cache to %program_path%\%fname%
dir /a %program_path%\%fname%
%uc% dump_cache>%program_path%\%fname%
echo ok
echo Loading cache from %program_path%\%fname%
type %program_path%\%fname%|%uc% load_cache
goto end
)

rem If file name specified; use this filename
:Defined
if /I "%arg1%" == "-s" (
echo Saving cache to %arg2%
%uc% dump_cache>%arg2%
dir /a %arg2%
echo ok
goto end
)

if /I "%arg1%" == "-l" (
echo Loading cache from %arg2%
dir /a %arg2%
type %arg2%|%uc% load_cache
goto end
)

if /I "%arg1%" == "-r" (
echo Saving cache to %arg2%
dir /a %arg2%
%uc% dump_cache>%arg2%
echo ok
echo Loading cache from %arg2%
type %arg2%|%uc% load_cache
goto end
)

:help
echo Usage: unbound_cache.cmd [-s] or [-l] or [-r] or [-h] [filename]
echo.
echo l - Load - default mode. Warming up Unbound DNS cache from saved file. cache-ttl must be high value.
echo s - Save - save Unbound DNS cache contents to plain file with domain names.
echo r - Reload - reloadind new cache entries and refresh existing cache
echo h - this screen.
echo filename - file to save/load dumped cache. If not specified, %program_path%\%fname% will be used instead.
echo Note: Run without any arguments will be in default mode.
echo       Also, unbound-control must be configured.
exit 1

:end
exit 0
