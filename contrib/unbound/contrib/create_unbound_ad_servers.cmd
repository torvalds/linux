@Echo off
rem Convert the Yoyo.org anti-ad server listing
rem into an unbound dns spoof redirection list.
rem Written by Y.Voinov (c) 2014

rem Note: Wget required!

rem Variables
set prefix="C:\Program Files (x86)"
set dst_dir=%prefix%\Unbound
set work_dir=%TEMP%
set list_addr="http://pgl.yoyo.org/adservers/serverlist.php?hostformat=nohtml&showintro=1&startdate%5Bday%5D=&startdate%5Bmonth%5D=&startdate%5Byear%5D="

rem Check Wget installed
for /f "delims=" %%a in ('where wget') do @set wget=%%a
if /I "%wget%"=="" echo Wget not found. If installed, add path to PATH environment variable. & exit 1
echo Wget found: %wget%

"%wget%" -O %work_dir%\yoyo_ad_servers %list_addr%

del /Q /F /S %dst_dir%\unbound_ad_servers

for /F "eol=; tokens=*" %%a in (%work_dir%\yoyo_ad_servers) do (
echo local-zone: %%a redirect>>%dst_dir%\unbound_ad_servers
echo local-data: "%%a A 127.0.0.1">>%dst_dir%\unbound_ad_servers
)

echo Done.
rem  then add an include line to your unbound.conf pointing to the full path of
rem  the unbound_ad_servers file:
rem
rem   include: $dst_dir/unbound_ad_servers
rem
