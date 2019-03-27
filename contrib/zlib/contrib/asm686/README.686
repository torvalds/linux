This is a patched version of zlib, modified to use
Pentium-Pro-optimized assembly code in the deflation algorithm. The
files changed/added by this patch are:

README.686
match.S

The speedup that this patch provides varies, depending on whether the
compiler used to build the original version of zlib falls afoul of the
PPro's speed traps. My own tests show a speedup of around 10-20% at
the default compression level, and 20-30% using -9, against a version
compiled using gcc 2.7.2.3. Your mileage may vary.

Note that this code has been tailored for the PPro/PII in particular,
and will not perform particuarly well on a Pentium.

If you are using an assembler other than GNU as, you will have to
translate match.S to use your assembler's syntax. (Have fun.)

Brian Raiter
breadbox@muppetlabs.com
April, 1998


Added for zlib 1.1.3:

The patches come from
http://www.muppetlabs.com/~breadbox/software/assembly.html

To compile zlib with this asm file, copy match.S to the zlib directory
then do:

CFLAGS="-O3 -DASMV" ./configure
make OBJA=match.o


Update:

I've been ignoring these assembly routines for years, believing that
gcc's generated code had caught up with it sometime around gcc 2.95
and the major rearchitecting of the Pentium 4. However, I recently
learned that, despite what I believed, this code still has some life
in it. On the Pentium 4 and AMD64 chips, it continues to run about 8%
faster than the code produced by gcc 4.1.

In acknowledgement of its continuing usefulness, I've altered the
license to match that of the rest of zlib. Share and Enjoy!

Brian Raiter
breadbox@muppetlabs.com
April, 2007
