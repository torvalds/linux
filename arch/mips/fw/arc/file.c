/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * ARC firmware interface.
 *
 * Copyright (C) 1994, 1995, 1996, 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/init.h>

#include <asm/fw/arc/types.h>
#include <asm/sgialib.h>

LONG
ArcGetDirectoryEntry(ULONG FileID, struct linux_vdirent *Buffer,
		     ULONG N, ULONG *Count)
{
	return ARC_CALL4(get_vdirent, FileID, Buffer, N, Count);
}

LONG
ArcOpen(CHAR *Path, enum linux_omode OpenMode, ULONG *FileID)
{
	return ARC_CALL3(open, Path, OpenMode, FileID);
}

LONG
ArcClose(ULONG FileID)
{
	return ARC_CALL1(close, FileID);
}

LONG
ArcRead(ULONG FileID, VOID *Buffer, ULONG N, ULONG *Count)
{
	return ARC_CALL4(read, FileID, Buffer, N, Count);
}

LONG
ArcGetReadStatus(ULONG FileID)
{
	return ARC_CALL1(get_rstatus, FileID);
}

LONG
ArcWrite(ULONG FileID, PVOID Buffer, ULONG N, PULONG Count)
{
	return ARC_CALL4(write, FileID, Buffer, N, Count);
}

LONG
ArcSeek(ULONG FileID, struct linux_bigint *Position, enum linux_seekmode SeekMode)
{
	return ARC_CALL3(seek, FileID, Position, SeekMode);
}

LONG
ArcMount(char *name, enum linux_mountops op)
{
	return ARC_CALL2(mount, name, op);
}

LONG
ArcGetFileInformation(ULONG FileID, struct linux_finfo *Information)
{
	return ARC_CALL2(get_finfo, FileID, Information);
}

LONG ArcSetFileInformation(ULONG FileID, ULONG AttributeFlags,
			   ULONG AttributeMask)
{
	return ARC_CALL3(set_finfo, FileID, AttributeFlags, AttributeMask);
}
