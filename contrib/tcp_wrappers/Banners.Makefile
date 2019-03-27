# @(#) Banners.Makefile 1.3 97/02/12 02:13:18
#
# Install this file as the Makefile in your directory with banner files.
# It will convert a prototype banner text to a form that is suitable for
# the ftp, telnet, rlogin, and other services. 
# 
# You'll have to comment out the IN definition below if your daemon
# names don't start with `in.'.
#
# The prototype text should live in the banners directory, as a file with
# the name "prototype". In the prototype text you can use %<character>
# sequences as described in the hosts_access.5 manual page (`nroff -man'
# format).  The sequences will be expanded while the banner message is
# sent to the client. For example:
#
#	Hello %u@%h, what brings you here?
#
# Expands to: Hello username@hostname, what brings you here? Note: the
# use of %u forces a client username lookup.
#
# In order to use banners, build the tcp wrapper with -DPROCESS_OPTIONS
# and use hosts.allow rules like this:
#
#	daemons ... : clients ... : banners /some/directory ...
#
# Of course, nothing prevents you from using multiple banner directories.
# For example, one banner directory for clients that are granted service,
# one banner directory for rejected clients, and one banner directory for
# clients with a hostname problem.
#
SHELL	= /bin/sh
IN	= in.
BANNERS	= $(IN)telnetd $(IN)ftpd $(IN)rlogind # $(IN)fingerd $(IN)rshd

all:	$(BANNERS)

$(IN)telnetd: prototype
	cp prototype $@
	chmod 644 $@

$(IN)ftpd: prototype
	sed 's/^/220-/' prototype > $@
	chmod 644 $@

$(IN)rlogind: prototype nul
	( ./nul ; cat prototype ) > $@
	chmod 644 $@

# Other services: banners may interfere with normal operation
# so they should probably be used only when refusing service.
# In particular, banners don't work with standard rsh daemons.
# You would have to use an rshd that has built-in tcp wrapper
# support, for example the rshd that is part of the logdaemon
# utilities.

$(IN)fingerd: prototype
	cp prototype $@
	chmod 644 $@

$(IN)rshd: prototype nul
	( ./nul ; cat prototype ) > $@
	chmod 644 $@

# In case no /dev/zero available, let's hope they have at least
# a C compiler of some sort.

nul:
	echo 'main() { write(1,"",1); return(0); }' >nul.c
	$(CC) $(CFLAGS) -s -o nul nul.c
	rm -f nul.c
