objects.txt syntax
------------------

To cover all the naming hacks that were previously in objects.h needed some
kind of hacks in objects.txt.

The basic syntax for adding an object is as follows:

	1 2 3 4		: shortName	: Long Name

		If Long Name contains only word characters and hyphen-minus
		(0x2D) or full stop (0x2E) then Long Name is used as basis
		for the base name in C. Otherwise, the shortName is used.

		The base name (let's call it 'base') will then be used to
		create the C macros SN_base, LN_base, NID_base and OBJ_base.

		Note that if the base name contains spaces, dashes or periods,
		those will be converted to underscore.

Then there are some extra commands:

	!Alias foo 1 2 3 4

		This just makes a name foo for an OID.  The C macro
		OBJ_foo will be created as a result.

	!Cname foo

		This makes sure that the name foo will be used as base name
		in C.

	!module foo
	1 2 3 4		: shortName	: Long Name
	!global

		The !module command was meant to define a kind of modularity.
		What it does is to make sure the module name is prepended
		to the base name.  !global turns this off.  This construction
		is not recursive.

Lines starting with # are treated as comments, as well as any line starting
with ! and not matching the commands above.

