.. SPDX-License-Identifier: GPL-2.0

===========
IP-Aliasing
===========

IP-aliases are an obsolete way to manage multiple IP-addresses/masks
per interface. Newer tools such as iproute2 support multiple
address/prefixes per interface, but aliases are still supported
for backwards compatibility.

An alias is formed by adding a colon and a string when running ifconfig.
This string is usually numeric, but this is not a must.


Alias creation
==============

Alias creation is done by 'magic' interface naming: eg. to create a
200.1.1.1 alias for eth0 ...
::

  # ifconfig eth0:0 200.1.1.1  etc,etc....
	~~ -> request alias #0 creation (if not yet exists) for eth0

The corresponding route is also set up by this command.  Please note:
The route always points to the base interface.


Alias deletion
==============

The alias is removed by shutting the alias down::

  # ifconfig eth0:0 down
	~~~~~~~~~~ -> will delete alias


Alias (re-)configuring
======================

Aliases are not real devices, but programs should be able to configure
and refer to them as usual (ifconfig, route, etc).


Relationship with main device
=============================

If the base device is shut down the added aliases will be deleted too.
