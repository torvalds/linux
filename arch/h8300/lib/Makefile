#
# Makefile for H8/300-specific library files..
#

.S.o:
	$(CC) $(AFLAGS) -D__ASSEMBLY__ -c $< -o $@

lib-y  = ashrdi3.o checksum.o memcpy.o memset.o abs.o romfs.o
