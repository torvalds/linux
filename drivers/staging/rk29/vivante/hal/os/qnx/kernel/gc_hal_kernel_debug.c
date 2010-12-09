/****************************************************************************
*
*    Copyright (c) 2005 - 2010 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************
*
*    Auto-generated file on 12/8/2010. Do not edit!!!
*
*****************************************************************************/




#include "gc_hal_kernel_qnx.h"
#include <stdarg.h>

/*
	gcdBUFFERED_OUTPUT

	When set to non-zero, all output is collected into a buffer with the
	specified size.  Once the buffer gets full, or the token "$$FLUSH$$" has
	been received, the debug buffer will be printed to the console.
*/
#define gcdBUFFERED_OUTPUT	0

/******************************************************************************\
******************************** Debug Variables *******************************
\******************************************************************************/

static gceSTATUS _lastError  = gcvSTATUS_OK;
static gctUINT32 _debugLevel = gcvLEVEL_ERROR;
static gctUINT32 _debugZones = gcvZONE_NONE;
static gctINT    _indent     = 0;

static void
OutputDebugString(
	IN gctCONST_STRING String
	)
{
#if gcdBUFFERED_OUTPUT
	static gctCHAR outputBuffer[gcdBUFFERED_OUTPUT];
	static gctINT outputBufferIndex = 0;
	gctINT n, i;

	n = (String != gcvNULL) ? strlen(String) + 1 : 0;

	if ((n == 0) || (outputBufferIndex + n > gcmSIZEOF(outputBuffer)))
	{
		for (i = 0; i < outputBufferIndex; i += strlen(outputBuffer + i) + 1)
		{
			printf(outputBuffer + i);
		}

		outputBufferIndex = 0;
	}

	if (n > 0)
	{
		memcpy(outputBuffer + outputBufferIndex, String, n);
		outputBufferIndex += n;
	}
#else
	if (String != gcvNULL)
	{
		printf(String);
	}
#endif
}

static void
_Print(
	IN gctCONST_STRING Message,
	IN va_list Arguments
	)
{
	char buffer[1024];
	int i, n;

	if (strcmp(Message, "$$FLUSH$$") == 0)
	{
		OutputDebugString(gcvNULL);
		return;
	}

	if (strncmp(Message, "--", 2) == 0)
	{
		if (_indent == 0)
		{
			printf("ERROR: _indent=0\n");
		}

		_indent -= 2;
	}

	for (i = 0; i < _indent; ++i)
	{
		buffer[i] = ' ';
	}

	/* Print message to buffer. */
	n = vsnprintf(buffer + i, sizeof(buffer) - i, Message, Arguments);
	if ((n <= 0) || (buffer[i + n - 1] != '\n'))
	{
		/* Append new-line. */
		strncat(buffer, "\n", sizeof(buffer));
	}

	/* Output to debugger. */
	OutputDebugString(buffer);

	if (strncmp(Message, "++", 2) == 0)
	{
		_indent += 2;
	}
}

/******************************************************************************\
********************************* Debug Macros *********************************
\******************************************************************************/

#define _DEBUGPRINT(Message) \
{ \
	va_list arguments; \
	\
	va_start(arguments, Message); \
	_Print(Message, arguments); \
	va_end(arguments); \
}

/******************************************************************************\
********************************** Debug Code **********************************
\******************************************************************************/

/*******************************************************************************
**
**	gckOS_Print
**
**	Send a message to the debugger.
**
**	INPUT:
**
**		gctCONST_STRING Message
**			Pointer to message.
**
**		...
**			Optional arguments.
**
**	OUTPUT:
**
**		Nothing.
*/

void
gckOS_Print(
	IN gctCONST_STRING Message,
	...
	)
{
	_DEBUGPRINT(Message);
}

/*******************************************************************************
**
**	gckOS_DebugTrace
**
**	Send a leveled message to the debugger.
**
**	INPUT:
**
**		gctUINT32 Level
**			Debug level of message.
**
**		gctCONST_STRING Message
**			Pointer to message.
**
**		...
**			Optional arguments.
**
**	OUTPUT:
**
**		Nothing.
*/

void
gckOS_DebugTrace(
	IN gctUINT32 Level,
	IN gctCONST_STRING Message,
	...
	)
{
	if (Level > _debugLevel)
	{
		return;
	}

	_DEBUGPRINT(Message);
}

/*******************************************************************************
**
**	gckOS_DebugTraceZone
**
**	Send a leveled and zoned message to the debugger.
**
**	INPUT:
**
**		gctUINT32 Level
**			Debug level for message.
**
**		gctUINT32 Zone
**			Debug zone for message.
**
**		gctCONST_STRING Message
**			Pointer to message.
**
**		...
**			Optional arguments.
**
**	OUTPUT:
**
**		Nothing.
*/

void
gckOS_DebugTraceZone(
	IN gctUINT32 Level,
	IN gctUINT32 Zone,
	IN gctCONST_STRING Message,
	...
	)
{
	if ((Level > _debugLevel) || !(Zone & _debugZones))
	{
		return;
	}

	_DEBUGPRINT(Message);
}

/*******************************************************************************
**
**	gckOS_DebugBreak
**
**	Break into the debugger.
**
**	INPUT:
**
**		Nothing.
**
**	OUTPUT:
**
**		Nothing.
*/
void
gckOS_DebugBreak(
	void
	)
{
	gckOS_DebugTrace(gcvLEVEL_ERROR, "gckOS_DebugBreak");
}

/*******************************************************************************
**
**	gckOS_DebugFatal
**
**	Send a message to the debugger and break into the debugger.
**
**	INPUT:
**
**		gctCONST_STRING Message
**			Pointer to message.
**
**		...
**			Optional arguments.
**
**	OUTPUT:
**
**		Nothing.
*/
void
gckOS_DebugFatal(
	IN gctCONST_STRING Message,
	...
	)
{
	_DEBUGPRINT(Message);

	/* Break into the debugger. */
	gckOS_DebugBreak();
}

/*******************************************************************************
**
**	gckOS_SetDebugLevel
**
**	Set the debug level.
**
**	INPUT:
**
**		gctUINT32 Level
**			New debug level.
**
**	OUTPUT:
**
**		Nothing.
*/

void
gckOS_SetDebugLevel(
	IN gctUINT32 Level
	)
{
	_debugLevel = Level;
}

/*******************************************************************************
**
**	gckOS_SetDebugZone
**
**	Set the debug zone.
**
**	INPUT:
**
**		gctUINT32 Zone
**			New debug zone.
**
**	OUTPUT:
**
**		Nothing.
*/
void
gckOS_SetDebugZone(
	IN gctUINT32 Zone
	)
{
	_debugZones = Zone;
}

/*******************************************************************************
**
**	gckOS_SetDebugLevelZone
**
**	Set the debug level and zone.
**
**	INPUT:
**
**		gctUINT32 Level
**			New debug level.
**
**		gctUINT32 Zone
**			New debug zone.
**
**	OUTPUT:
**
**		Nothing.
*/

void
gckOS_SetDebugLevelZone(
	IN gctUINT32 Level,
	IN gctUINT32 Zone
	)
{
	_debugLevel = Level;
	_debugZones = Zone;
}

/*******************************************************************************
**
**	gckOS_SetDebugZones
**
**	Enable or disable debug zones.
**
**	INPUT:
**
**		gctUINT32 Zones
**			Debug zones to enable or disable.
**
**		gctBOOL Enable
**			Set to gcvTRUE to enable the zones (or the Zones with the current
**			zones) or gcvFALSE to disable the specified Zones.
**
**	OUTPUT:
**
**		Nothing.
*/

void
gckOS_SetDebugZones(
	IN gctUINT32 Zones,
	IN gctBOOL Enable
	)
{
	if (Enable)
	{
		/* Enable the zones. */
		_debugZones |= Zones;
	}
	else
	{
		/* Disable the zones. */
		_debugZones &= ~Zones;
	}
}

/*******************************************************************************
**
**	gckOS_Verify
**
**	Called to verify the result of a function call.
**
**	INPUT:
**
**		gceSTATUS Status
**			Function call result.
**
**	OUTPUT:
**
**		Nothing.
*/

void
gckOS_Verify(
	IN gceSTATUS Status
	)
{
	_lastError = Status;
}

