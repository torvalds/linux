divert(-1)
#
# Copyright (c) 1983 Eric P. Allman
# Copyright (c) 1988, 1993
#	The Regents of the University of California.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All advertising materials mentioning features or use of this software
#    must display the following acknowledgement:
#	This product includes software developed by the University of
#	California, Berkeley and its contributors.
# 4. Neither the name of the University nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

#
#  This is a generic configuration file for FreeBSD 6.X and later systems.
#  If you want to customize it, copy it to a name appropriate for your
#  environment and do the modifications there.
#
#  The best documentation for this .mc file is:
#  /usr/share/sendmail/cf/README or
#  /usr/src/contrib/sendmail/cf/README
# 
#  NOTE: If you enable RunAsUser, make sure that you adjust the permissions
#  and owner of the SSL certificates and keys in /etc/mail/certs to be usable
#  by that user.
#

divert(0)
VERSIONID(`$FreeBSD$')
OSTYPE(freebsd6)
DOMAIN(generic)

FEATURE(access_db, `hash -o -T<TMPF> /etc/mail/access')
FEATURE(blacklist_recipients)
FEATURE(local_lmtp)
FEATURE(mailertable, `hash -o /etc/mail/mailertable')
FEATURE(virtusertable, `hash -o /etc/mail/virtusertable')

dnl Enable STARTTLS for receiving email.
define(`CERT_DIR', `/etc/mail/certs')dnl
define(`confSERVER_CERT', `CERT_DIR/host.cert')dnl
define(`confSERVER_KEY', `CERT_DIR/host.key')dnl
define(`confCLIENT_CERT', `CERT_DIR/host.cert')dnl
define(`confCLIENT_KEY', `CERT_DIR/host.key')dnl
define(`confCACERT', `CERT_DIR/cacert.pem')dnl
define(`confCACERT_PATH', `CERT_DIR')dnl
define(`confDH_PARAMETERS', `CERT_DIR/dh.param')dnl

dnl Uncomment to allow relaying based on your MX records.
dnl NOTE: This can allow sites to use your server as a backup MX without
dnl       your permission.
dnl FEATURE(relay_based_on_MX)

dnl DNS based black hole lists
dnl --------------------------------
dnl DNS based black hole lists come and go on a regular basis
dnl so this file will not serve as a database of the available servers.
dnl For more information, visit
dnl http://en.wikipedia.org/wiki/DNSBL

dnl Uncomment to activate your chosen DNS based blacklist
dnl FEATURE(dnsbl, `dnsbl.example.com')
dnl Alternatively, you can provide your own server and rejection message:
dnl FEATURE(dnsbl, `dnsbl.example.com', ``"550 Mail from " $&{client_addr} " rejected"'')

dnl Dialup users should uncomment and define this appropriately
dnl define(`SMART_HOST', `your.isp.mail.server')

dnl Uncomment the first line to change the location of the default
dnl /etc/mail/local-host-names and comment out the second line.
dnl define(`confCW_FILE', `-o /etc/mail/sendmail.cw')
define(`confCW_FILE', `-o /etc/mail/local-host-names')

dnl Enable for both IPv4 and IPv6 (optional)
DAEMON_OPTIONS(`Name=IPv4, Family=inet')
DAEMON_OPTIONS(`Name=IPv6, Family=inet6, Modifiers=O')

define(`confBIND_OPTS', `WorkAroundBrokenAAAA')
define(`confNO_RCPT_ACTION', `add-to-undisclosed')
define(`confPRIVACY_FLAGS', `authwarnings,noexpn,novrfy')
MAILER(local)
MAILER(smtp)
