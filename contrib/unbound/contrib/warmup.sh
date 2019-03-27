#!/bin/sh

# --------------------------------------------------------------
# -- Warm up DNS cache script by your own MRU domains or from
# -- file when it specified as script argument.
# --
# -- Version 1.1
# -- By Yuri Voinov (c) 2014
# --------------------------------------------------------------

# Default DNS host address
address="127.0.0.1"

cat=`which cat`
dig=`which dig`

if [ -z "$1" ]; then
echo "Warming up cache by MRU domains..."
$dig -f - @$address >/dev/null 2>&1 <<EOT
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
clamav.net
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
EOT
else
 echo "Warming up cache from $1 file..."
 $cat $1 | $dig -f - @$address >/dev/null 2>&1 
fi

echo "Done."

echo "Saving cache..."
script=`which unbound_cache.sh`
[ -f "$script" ] && $script -s
echo "Done."

exit 0
