hostapd - user space IEEE 802.11 AP and IEEE 802.1X/WPA/WPA2/EAP
	  Authenticator and RADIUS authentication server
================================================================

Copyright (c) 2002-2011, Jouni Malinen <j@w1.fi> and contributors
All Rights Reserved.

This program is dual-licensed under both the GPL version 2 and BSD
license. Either license may be used at your option.



License
-------

GPL v2:

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

(this copy of the license is in COPYING file)


Alternatively, this software may be distributed, used, and modified
under the terms of BSD license:

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

3. Neither the name(s) of the above-listed copyright holder(s) nor the
   names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.



Introduction
============

Originally, hostapd was an optional user space component for Host AP
driver. It adds more features to the basic IEEE 802.11 management
included in the kernel driver: using external RADIUS authentication
server for MAC address based access control, IEEE 802.1X Authenticator
and dynamic WEP keying, RADIUS accounting, WPA/WPA2 (IEEE 802.11i/RSN)
Authenticator and dynamic TKIP/CCMP keying.

The current version includes support for other drivers, an integrated
EAP server (i.e., allow full authentication without requiring
an external RADIUS authentication server), and RADIUS authentication
server for EAP authentication.


Requirements
------------

Current hardware/software requirements:
- drivers:
	Host AP driver for Prism2/2.5/3.
	(http://hostap.epitest.fi/)
	Please note that station firmware version needs to be 1.7.0 or newer
	to work in WPA mode.

	madwifi driver for cards based on Atheros chip set (ar521x)
	(http://sourceforge.net/projects/madwifi/)
	Please note that you will need to add the correct path for
	madwifi driver root directory in .config (see defconfig file for
	an example: CFLAGS += -I<path>)

	mac80211-based drivers that support AP mode (with driver=nl80211).
	This includes drivers for Atheros (ath9k) and Broadcom (b43)
	chipsets.

	Any wired Ethernet driver for wired IEEE 802.1X authentication
	(experimental code)

	FreeBSD -current (with some kernel mods that have not yet been
	committed when hostapd v0.3.0 was released)
	BSD net80211 layer (e.g., Atheros driver)


Build configuration
-------------------

In order to be able to build hostapd, you will need to create a build
time configuration file, .config that selects which optional
components are included. See defconfig file for example configuration
and list of available options.



IEEE 802.1X
===========

IEEE Std 802.1X-2001 is a standard for port-based network access
control. In case of IEEE 802.11 networks, a "virtual port" is used
between each associated station and the AP. IEEE 802.11 specifies
minimal authentication mechanism for stations, whereas IEEE 802.1X
introduces a extensible mechanism for authenticating and authorizing
users.

IEEE 802.1X uses elements called Supplicant, Authenticator, Port
Access Entity, and Authentication Server. Supplicant is a component in
a station and it performs the authentication with the Authentication
Server. An access point includes an Authenticator that relays the packets
between a Supplicant and an Authentication Server. In addition, it has a
Port Access Entity (PAE) with Authenticator functionality for
controlling the virtual port authorization, i.e., whether to accept
packets from or to the station.

IEEE 802.1X uses Extensible Authentication Protocol (EAP). The frames
between a Supplicant and an Authenticator are sent using EAP over LAN
(EAPOL) and the Authenticator relays these frames to the Authentication
Server (and similarly, relays the messages from the Authentication
Server to the Supplicant). The Authentication Server can be colocated with the
Authenticator, in which case there is no need for additional protocol
for EAP frame transmission. However, a more common configuration is to
use an external Authentication Server and encapsulate EAP frame in the
frames used by that server. RADIUS is suitable for this, but IEEE
802.1X would also allow other mechanisms.

Host AP driver includes PAE functionality in the kernel driver. It
is a relatively simple mechanism for denying normal frames going to
or coming from an unauthorized port. PAE allows IEEE 802.1X related
frames to be passed between the Supplicant and the Authenticator even
on an unauthorized port.

User space daemon, hostapd, includes Authenticator functionality. It
receives 802.1X (EAPOL) frames from the Supplicant using the wlan#ap
device that is also used with IEEE 802.11 management frames. The
frames to the Supplicant are sent using the same device.

The normal configuration of the Authenticator would use an external
Authentication Server. hostapd supports RADIUS encapsulation of EAP
packets, so the Authentication Server should be a RADIUS server, like
FreeRADIUS (http://www.freeradius.org/). The Authenticator in hostapd
relays the frames between the Supplicant and the Authentication
Server. It also controls the PAE functionality in the kernel driver by
controlling virtual port authorization, i.e., station-AP
connection, based on the IEEE 802.1X state.

When a station would like to use the services of an access point, it
will first perform IEEE 802.11 authentication. This is normally done
with open systems authentication, so there is no security. After
this, IEEE 802.11 association is performed. If IEEE 802.1X is
configured to be used, the virtual port for the station is set in
Unauthorized state and only IEEE 802.1X frames are accepted at this
point. The Authenticator will then ask the Supplicant to authenticate
with the Authentication Server. After this is completed successfully,
the virtual port is set to Authorized state and frames from and to the
station are accepted.

Host AP configuration for IEEE 802.1X
-------------------------------------

The user space daemon has its own configuration file that can be used to
define AP options. Distribution package contains an example
configuration file (hostapd/hostapd.conf) that can be used as a basis
for configuration. It includes examples of all supported configuration
options and short description of each option. hostapd should be started
with full path to the configuration file as the command line argument,
e.g., './hostapd /etc/hostapd.conf'. If you have more that one wireless
LAN card, you can use one hostapd process for multiple interfaces by
giving a list of configuration files (one per interface) in the command
line.

hostapd includes a minimal co-located IEEE 802.1X server which can be
used to test IEEE 802.1X authentication. However, it should not be
used in normal use since it does not provide any security. This can be
configured by setting ieee8021x and minimal_eap options in the
configuration file.

An external Authentication Server (RADIUS) is configured with
auth_server_{addr,port,shared_secret} options. In addition,
ieee8021x and own_ip_addr must be set for this mode. With such
configuration, the co-located Authentication Server is not used and EAP
frames will be relayed using EAPOL between the Supplicant and the
Authenticator and RADIUS encapsulation between the Authenticator and
the Authentication Server. Other than this, the functionality is similar
to the case with the co-located Authentication Server.

Authentication Server and Supplicant
------------------------------------

Any RADIUS server supporting EAP should be usable as an IEEE 802.1X
Authentication Server with hostapd Authenticator. FreeRADIUS
(http://www.freeradius.org/) has been successfully tested with hostapd
Authenticator and both Xsupplicant (http://www.open1x.org) and Windows
XP Supplicants. EAP/TLS was used with Xsupplicant and
EAP/MD5-Challenge with Windows XP.

http://www.missl.cs.umd.edu/wireless/eaptls/ has useful information
about using EAP/TLS with FreeRADIUS and Xsupplicant (just replace
Cisco access point with Host AP driver, hostapd daemon, and a Prism2
card ;-). http://www.freeradius.org/doc/EAP-MD5.html has information
about using EAP/MD5 with FreeRADIUS, including instructions for WinXP
configuration. http://www.denobula.com/EAPTLS.pdf has a HOWTO on
EAP/TLS use with WinXP Supplicant.

Automatic WEP key configuration
-------------------------------

EAP/TLS generates a session key that can be used to send WEP keys from
an AP to authenticated stations. The Authenticator in hostapd can be
configured to automatically select a random default/broadcast key
(shared by all authenticated stations) with wep_key_len_broadcast
option (5 for 40-bit WEP or 13 for 104-bit WEP). In addition,
wep_key_len_unicast option can be used to configure individual unicast
keys for stations. This requires support for individual keys in the
station driver.

WEP keys can be automatically updated by configuring rekeying. This
will improve security of the network since same WEP key will only be
used for a limited period of time. wep_rekey_period option sets the
interval for rekeying in seconds.


WPA/WPA2
========

Features
--------

Supported WPA/IEEE 802.11i features:
- WPA-PSK ("WPA-Personal")
- WPA with EAP (e.g., with RADIUS authentication server) ("WPA-Enterprise")
- key management for CCMP, TKIP, WEP104, WEP40
- RSN/WPA2 (IEEE 802.11i), including PMKSA caching and pre-authentication

WPA
---

The original security mechanism of IEEE 802.11 standard was not
designed to be strong and has proved to be insufficient for most
networks that require some kind of security. Task group I (Security)
of IEEE 802.11 working group (http://www.ieee802.org/11/) has worked
to address the flaws of the base standard and has in practice
completed its work in May 2004. The IEEE 802.11i amendment to the IEEE
802.11 standard was approved in June 2004 and this amendment is likely
to be published in July 2004.

Wi-Fi Alliance (http://www.wi-fi.org/) used a draft version of the
IEEE 802.11i work (draft 3.0) to define a subset of the security
enhancements that can be implemented with existing wlan hardware. This
is called Wi-Fi Protected Access<TM> (WPA). This has now become a
mandatory component of interoperability testing and certification done
by Wi-Fi Alliance. Wi-Fi provides information about WPA at its web
site (http://www.wi-fi.org/OpenSection/protected_access.asp).

IEEE 802.11 standard defined wired equivalent privacy (WEP) algorithm
for protecting wireless networks. WEP uses RC4 with 40-bit keys,
24-bit initialization vector (IV), and CRC32 to protect against packet
forgery. All these choices have proven to be insufficient: key space is
too small against current attacks, RC4 key scheduling is insufficient
(beginning of the pseudorandom stream should be skipped), IV space is
too small and IV reuse makes attacks easier, there is no replay
protection, and non-keyed authentication does not protect against bit
flipping packet data.

WPA is an intermediate solution for the security issues. It uses
Temporal Key Integrity Protocol (TKIP) to replace WEP. TKIP is a
compromise on strong security and possibility to use existing
hardware. It still uses RC4 for the encryption like WEP, but with
per-packet RC4 keys. In addition, it implements replay protection,
keyed packet authentication mechanism (Michael MIC).

Keys can be managed using two different mechanisms. WPA can either use
an external authentication server (e.g., RADIUS) and EAP just like
IEEE 802.1X is using or pre-shared keys without need for additional
servers. Wi-Fi calls these "WPA-Enterprise" and "WPA-Personal",
respectively. Both mechanisms will generate a master session key for
the Authenticator (AP) and Supplicant (client station).

WPA implements a new key handshake (4-Way Handshake and Group Key
Handshake) for generating and exchanging data encryption keys between
the Authenticator and Supplicant. This handshake is also used to
verify that both Authenticator and Supplicant know the master session
key. These handshakes are identical regardless of the selected key
management mechanism (only the method for generating master session
key changes).


IEEE 802.11i / WPA2
-------------------

The design for parts of IEEE 802.11i that were not included in WPA has
finished (May 2004) and this amendment to IEEE 802.11 was approved in
June 2004. Wi-Fi Alliance is using the final IEEE 802.11i as a new
version of WPA called WPA2. This includes, e.g., support for more
robust encryption algorithm (CCMP: AES in Counter mode with CBC-MAC)
to replace TKIP and optimizations for handoff (reduced number of
messages in initial key handshake, pre-authentication, and PMKSA caching).

Some wireless LAN vendors are already providing support for CCMP in
their WPA products. There is no "official" interoperability
certification for CCMP and/or mixed modes using both TKIP and CCMP, so
some interoperability issues can be expected even though many
combinations seem to be working with equipment from different vendors.
Testing for WPA2 is likely to start during the second half of 2004.

hostapd configuration for WPA/WPA2
----------------------------------

TODO

# Enable WPA. Setting this variable configures the AP to require WPA (either
# WPA-PSK or WPA-RADIUS/EAP based on other configuration). For WPA-PSK, either
# wpa_psk or wpa_passphrase must be set and wpa_key_mgmt must include WPA-PSK.
# For WPA-RADIUS/EAP, ieee8021x must be set (but without dynamic WEP keys),
# RADIUS authentication server must be configured, and WPA-EAP must be included
# in wpa_key_mgmt.
# This field is a bit field that can be used to enable WPA (IEEE 802.11i/D3.0)
# and/or WPA2 (full IEEE 802.11i/RSN):
# bit0 = WPA
# bit1 = IEEE 802.11i/RSN (WPA2)
#wpa=1

# WPA pre-shared keys for WPA-PSK. This can be either entered as a 256-bit
# secret in hex format (64 hex digits), wpa_psk, or as an ASCII passphrase
# (8..63 characters) that will be converted to PSK. This conversion uses SSID
# so the PSK changes when ASCII passphrase is used and the SSID is changed.
#wpa_psk=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
#wpa_passphrase=secret passphrase

# Set of accepted key management algorithms (WPA-PSK, WPA-EAP, or both). The
# entries are separated with a space.
#wpa_key_mgmt=WPA-PSK WPA-EAP

# Set of accepted cipher suites (encryption algorithms) for pairwise keys
# (unicast packets). This is a space separated list of algorithms:
# CCMP = AES in Counter mode with CBC-MAC [RFC 3610, IEEE 802.11i]
# TKIP = Temporal Key Integrity Protocol [IEEE 802.11i]
# Group cipher suite (encryption algorithm for broadcast and multicast frames)
# is automatically selected based on this configuration. If only CCMP is
# allowed as the pairwise cipher, group cipher will also be CCMP. Otherwise,
# TKIP will be used as the group cipher.
#wpa_pairwise=TKIP CCMP

# Time interval for rekeying GTK (broadcast/multicast encryption keys) in
# seconds.
#wpa_group_rekey=600

# Time interval for rekeying GMK (master key used internally to generate GTKs
# (in seconds).
#wpa_gmk_rekey=86400

# Enable IEEE 802.11i/RSN/WPA2 pre-authentication. This is used to speed up
# roaming be pre-authenticating IEEE 802.1X/EAP part of the full RSN
# authentication and key handshake before actually associating with a new AP.
#rsn_preauth=1
#
# Space separated list of interfaces from which pre-authentication frames are
# accepted (e.g., 'eth0' or 'eth0 wlan0wds0'. This list should include all
# interface that are used for connections to other APs. This could include
# wired interfaces and WDS links. The normal wireless data interface towards
# associated stations (e.g., wlan0) should not be added, since
# pre-authentication is only used with APs other than the currently associated
# one.
#rsn_preauth_interfaces=eth0
