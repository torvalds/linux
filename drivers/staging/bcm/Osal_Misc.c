	/*++

	Copyright (c) Beceem Communications Inc.

	Module Name:
		WIN_Misc.c

	Abstract:
		Implements the Miscelanneous OS Construts
			Linked Lists
			Dispatcher Objects(Events,Semaphores,Spin Locks and the like)
			Files

	Revision History:
		Who         When        What
		--------    --------    ----------------------------------------------
		Name		Date		Created/reviewed/modified
		Rajeev		24/1/08		Created
	Notes:

	--*/
#include "headers.h"

bool OsalMemCompare(void *dest, void *src, UINT len)
{
	return (memcmp(src, dest, len));
}
