Return-Path: sanders@austin.BSDI.COM
Received: from hofmann.CS.Berkeley.EDU (hofmann.CS.Berkeley.EDU [128.32.34.35]) by orodruin.CS.Berkeley.EDU (8.6.9/8.7.0.Beta0) with ESMTP id KAA28278 for <eric@orodruin.CS.Berkeley.EDU>; Sat, 10 Dec 1994 10:49:08 -0800
Received: from austin.BSDI.COM (austin.BSDI.COM [137.39.95.2]) by hofmann.CS.Berkeley.EDU (8.6.9/8.6.6.Beta11) with ESMTP id KAA09482 for <eric@cs.berkeley.edu>; Sat, 10 Dec 1994 10:49:03 -0800
Received: from austin.BSDI.COM (sanders@localhost [127.0.0.1]) by austin.BSDI.COM (8.6.9/8.6.9) with ESMTP id MAA14919 for <eric@cs.berkeley.edu>; Sat, 10 Dec 1994 12:49:01 -0600
Message-Id: <199412101849.MAA14919@austin.BSDI.COM>
To: Eric Allman <eric@cs.berkeley.edu>
Subject: Re: sorting mailings lists with fastest delivery users first 
In-reply-to: Your message of Sat, 10 Dec 1994 08:25:30 PST.
References: <199412101625.IAA15407@mastodon.CS.Berkeley.EDU> 
From: Tony Sanders <sanders@bsdi.com>
Organization: Berkeley Software Design, Inc.
Date: Sat, 10 Dec 1994 12:49:00 -0600
Sender: sanders@austin.BSDI.COM

(some random text deleted)

I'll send you something else I've hacked up.  You are free to use this
or do with it as you like (I hereby make all my parts public domain).
It's a sample .mc file that has comments (mostly taken from the README)
and examples describing most of the common things people need to setup.

#
# /usr/share/sendmail/cf/sample.mc
#
# Do not edit /etc/sendmail.cf directly unless you cannot do what you
# want in the master config file (/usr/share/sendmail/cf/sample.mc).
# To create /etc/sendmail.cf from the master:
#     cd /usr/share/sendmail/cf
#     mv /etc/sendmail.cf /etc/sendmail.cf.save
#     m4 < sample.mc > /etc/sendmail.cf
#
# Then kill and restart sendmail:
#     sh -c 'set `cat /var/run/sendmail.pid`; kill $1; shift; eval "$@"'
#
# See /usr/share/sendmail/README for help in building a configuration file.
#
include(`../m4/cf.m4')
VERSIONID(`@(#)$Id: bsdi.mc,v 8.1 1999-02-06 18:44:08 gshapiro Exp $')

dnl # Specify your OS type below
OSTYPE(`bsd4.4')

dnl # NOTE: `dnl' is the m4 command for delete-to-newline; these are
dnl # used to prevent those lines from appearing in the sendmail.cf.
dnl #
dnl # UUCP-only sites should configure FEATURE(`nodns') and SMART_HOST.
dnl # The uucp-dom mailer requires MAILER(smtp).  For more info, see
dnl # `UUCP Config' at the end of this file.

dnl # If you are not running DNS at all, it is important to use
dnl # FEATURE(nodns) to avoid having sendmail queue everything
dnl # waiting for the name server to come up.
dnl # Example:
dnl     FEATURE(`nodns')

dnl # Use FEATURE(`nocanonify') to skip address canonification via $[ ... $].
dnl # This would generally only be used by sites that only act as mail gateways
dnl # or which have user agents that do full canonification themselves.
dnl # You may also want to use:
dnl #     define(`confBIND_OPTS',`-DNSRCH -DEFNAMES')
dnl # to turn off the usual resolver options that do a similar thing.
dnl # Examples:
dnl     FEATURE(`nocanonify')
dnl     define(`confBIND_OPTS',`-DNSRCH -DEFNAMES')

dnl # If /bin/hostname is not set to the FQDN (Full Qualified Domain Name;
dnl # for example, foo.bar.com) *and* you are not running a nameserver
dnl # (that is, you do not have an /etc/resolv.conf and are not running
dnl # named) *and* the canonical name for your machine in /etc/hosts
dnl # (the canonical name is the first name listed for a given IP Address)
dnl # is not the FQDN version then define NEED_DOMAIN and specify your
dnl # domain using `DD' (for example, if your hostname is `foo.bar.com'
dnl # then use DDbar.com).  If in doubt, just define it anyway; doesn't hurt.
dnl # Examples:
dnl     define(`NEED_DOMAIN', `1')
dnl     DDyour.site.domain

dnl # Define SMART_HOST if you want all outgoing mail to go to a central
dnl # site.  SMART_HOST applies to names qualified with non-local names.
dnl # Example:
dnl     define(`SMART_HOST', `smtp:firewall.bar.com')

dnl # Define MAIL_HUB if you want all incoming mail sent to a
dnl # centralized hub, as for a shared /var/spool/mail scheme.
dnl # MAIL_HUB applies to names qualified with the name of the
dnl # local host (e.g., "eric@foo.bar.com").
dnl # Example:
dnl     define(`MAIL_HUB', `smtp:mailhub.bar.com')

dnl # LOCAL_RELAY is a site that will handle unqualified names, this is
dnl # basically for site/company/department wide alias forwarding.  By
dnl # default mail is delivered on the local host.
dnl # Example:
dnl     define(`LOCAL_RELAY', `smtp:mailgate.bar.com')

dnl # Relay hosts for fake domains: .UUCP .BITNET .CSNET
dnl # Examples:
dnl     define(`UUCP_RELAY', `mailer:your_relay_host')
dnl     define(`BITNET_RELAY', `mailer:your_relay_host')
dnl     define(`CSNET_RELAY', `mailer:your_relay_host')

dnl # Define `MASQUERADE_AS' is used to hide behind a gateway.
dnl # add any accounts you wish to be exposed (i.e., not hidden) to the
dnl # `EXPOSED_USER' list.
dnl # Example:
dnl     MASQUERADE_AS(`some.other.host')

dnl # If masquerading, EXPOSED_USER defines the list of accounts
dnl # that retain the local hostname in their address.
dnl # Example:
dnl     EXPOSED_USER(`postmaster hostmaster webmaster')

dnl # If masquerading is enabled (using MASQUERADE_AS above) then
dnl # FEATURE(allmasquerade) will cause recipient addresses to
dnl # masquerade as being from the masquerade host instead of
dnl # getting the local hostname.  Although this may be right for
dnl # ordinary users, it breaks local aliases that aren't exposed
dnl # using EXPOSED_USER.
dnl # Example:
dnl     FEATURE(allmasquerade)

dnl # Include any required mailers
MAILER(local)
MAILER(smtp)
MAILER(uucp)

LOCAL_CONFIG
# If this machine should be accepting mail as local for other hostnames
# that are MXed to this hostname then add those hostnames below using
# a line like:
#     Cw bar.com
# The most common case where you need this is if this machine is supposed
# to be accepting mail for the domain.  That is, if this machine is
# foo.bar.com and you have an MX record in the DNS that looks like:
#     bar.com.  IN      MX      0 foo.bar.com.
# Then you will need to add `Cw bar.com' to the config file for foo.bar.com.
# DO NOT add Cw entries for hosts whom you simply store and forward mail
# for or else it will attempt local delivery.  So just because bubba.bar.com
# is MXed to your machine you should not add a `Cw bubba.bar.com' entry
# unless you want local delivery and your machine is the highest-priority
# MX entry (that is is has the lowest preference value in the DNS.

LOCAL_RULE_0
# `LOCAL_RULE_0' can be used to introduce alternate delivery rules.
# For example, let's say you accept mail via an MX record for widgets.com
# (don't forget to add widgets.com to your Cw list, as above).
#
# If wigets.com only has an AOL address (widgetsinc) then you could use:
# R$+ <@ widgets.com.>  $#smtp $@aol.com. $:widgetsinc<@aol.com.>
#
# Or, if widgets.com was connected to you via UUCP as the UUCP host
# widgets you might have:
# R$+ <@ widgets.com.>   $#uucp $@widgets $:$1<@widgets.com.>

dnl ###
dnl ### UUCP Config
dnl ###

dnl # `SITECONFIG(site_config_file, name_of_site, connection)'
dnl # site_config_file the name of a file in the cf/siteconfig
dnl #                  directory (less the `.m4')
dnl # name_of_site     the actual name of your UUCP site
dnl # connection       one of U, W, X, or Y; where U means the sites listed
dnl #                  in the config file are connected locally;  W, X, and Y
dnl #                  build remote UUCP hub classes ($=W, etc).
dnl # You will need to create the specific site_config_file in
dnl #     /usr/share/sendmail/siteconfig/site_config_file.m4
dnl # The site_config_file contains a list of directly connected UUCP hosts,
dnl # e.g., if you only connect to UUCP site gargoyle then you could just:
dnl #   echo 'SITE(gargoyle)' > /usr/share/sendmail/siteconfig/uucp.foobar.m4
dnl # Example:
dnl     SITECONFIG(`uucp.foobar', `foobar', U)

dnl # If you are on a local SMTP-based net that connects to the outside
dnl # world via UUCP, you can use LOCAL_NET_CONFIG to add appropriate rules.
dnl # For example:
dnl #   define(`SMART_HOST', suucp:uunet)
dnl #   LOCAL_NET_CONFIG
dnl #   R$* < @ $* .$m. > $*    $#smtp $@ $2.$m. $: $1 < @ $2.$m. > $3
dnl # This will cause all names that end in your domain name ($m) to be sent
dnl # via SMTP; anything else will be sent via suucp (smart UUCP) to uunet.
dnl # If you have FEATURE(nocanonify), you may need to omit the dots after
dnl # the $m.
dnl #
dnl # If you are running a local DNS inside your domain which is not
dnl # otherwise connected to the outside world, you probably want to use:
dnl #   define(`SMART_HOST', smtp:fire.wall.com)
dnl #   LOCAL_NET_CONFIG
dnl #   R$* < @ $* . > $*       $#smtp $@ $2. $: $1 < @ $2. > $3
dnl # That is, send directly only to things you found in your DNS lookup;
dnl # anything else goes through SMART_HOST.
