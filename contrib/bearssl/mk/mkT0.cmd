@echo off

rem =====================================================================
rem This script uses the command-line C# compiler csc.exe, which is
rem provided with the .NET framework. We need framework 3.5 or later
rem (some of the code uses features not available in the language version
rem implemented in the compiler provided with framework 2.0.50727).
rem =====================================================================

if exist "%SystemRoot%\Microsoft.NET\Framework\v3.5\csc.exe" (
	set CSC="%SystemRoot%\Microsoft.NET\Framework\v3.5\csc.exe"
	goto do_compile
)
if exist "%SystemRoot%\Microsoft.NET\Framework\v4.0.30319\csc.exe" (
	set CSC="%SystemRoot%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
	goto do_compile
)
if exist "%SystemRoot%\Microsoft.NET\Framework64\v3.5\csc.exe" (
	set CSC="%SystemRoot%\Microsoft.NET\Framework64\v3.5\csc.exe"
	goto do_compile
)
if exist "%SystemRoot%\Microsoft.NET\Framework64\v4.0.30319\csc.exe" (
	set CSC="%SystemRoot%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
	goto do_compile
)

echo C# compiler not found
exit 1

:do_compile
%CSC% /nologo /out:T0Comp.exe /main:T0Comp /res:T0\kern.t0,t0-kernel T0\*.cs
if %errorlevel% neq 0 exit /b %errorlevel%
