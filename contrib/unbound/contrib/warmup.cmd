@echo off

rem --------------------------------------------------------------
rem -- Warm up DNS cache script by your own MRU domains or from
rem -- file when it specified as script argument.
rem --
rem -- Version 1.1
rem -- By Yuri Voinov (c) 2014
rem --------------------------------------------------------------

rem DNS host address
set address="127.0.0.1"

rem Check dig installed
for /f "delims=" %%a in ('where dig') do @set dig=%%a
if /I "%dig%"=="" echo Dig not found. If installed, add path to PATH environment variable. & exit 1
echo Dig found: %dig%

set arg=%1%

if defined %arg% (goto builtin) else (goto from_file)

:builtin
echo Warming up cache by MRU domains...
for %%a in (
2gis.ru
admir.kz
adobe.com
agent.mail.ru
aimp.ru
akamai.com
akamai.net
almaty.tele2.kz
aol.com
apple.com
arin.com
artlebedev.ru
auto.mail.ru
beeline.kz
bing.com
blogspot.com
comodo.com
dnscrypt.org
drive.google.com
drive.mail.ru
facebook.com
farmanager.com
fb.com
firefox.com
forum.farmanager.com
gazeta.ru
getsharex.com
gismeteo.ru
google.com
google.kz
google.ru
googlevideo.com
goto.kz
iana.org
icq.com
imap.mail.ru
instagram.com
intel.com
irr.kz
java.com
kaspersky.com
kaspersky.ru
kcell.kz
krisha.kz
lady.mail.ru
lenta.ru
libreoffice.org
linkedin.com
livejournal.com
mail.google.com
mail.ru
microsoft.com
mozilla.org
mra.mail.ru
munin-monitoring.org
my.mail.ru
news.bbcimg.co.uk
news.mail.ru
newsimg.bbc.net.uk
nvidia.com
odnoklassniki.ru
ok.ru
opencsw.org
opendns.com
opendns.org
opennet.ru
opera.com
oracle.com
peerbet.ru
piriform.com
plugring.farmanager.com
privoxy.org
qip.ru
raidcall.com
rambler.ru
reddit.com
ru.wikipedia.org
shallalist.de
skype.com
snob.ru
squid-cache.org
squidclamav.darold.net
squidguard.org
ssl.comodo.com
ssl.verisign.com
symantec.com
symantecliveupdate.com
tele2.kz
tengrinews.kz
thunderbird.com
torproject.org
torstatus.blutmagie.de
translate.google.com
unbound.net
verisign.com
vk.com
vk.me
vk.ru
vkontakte.com
vkontakte.ru
vlc.org
watsapp.net
weather.mail.ru
windowsupdate.com
www.baidu.com
www.bbc.co.uk
www.internic.net
www.opennet.ru
www.topgear.com
ya.ru
yahoo.com
yandex.com
yandex.ru
youtube.com
ytimg.com
) do "%dig%" %%a @%address% 1>nul 2>nul
goto end

:from_file
echo Warming up cache from %1% file...
%dig% -f %arg% @%address% 1>nul 2>nul 

:end
echo Saving cache...
if exist unbound_cache.cmd unbound_cache.cmd -s
echo Done.

exit 0