#include "libgcc.h"

word_type __ucmpdi2(DWtype a, DWtype b)
{
	const DWunion au = {.ll = a};
	const DWunion bu = {.ll = b};

	if ((UWtype) au.s.high < (UWtype) bu.s.high)
		return 0;
	else if ((UWtype) au.s.high > (UWtype) bu.s.high)
		return 2;
	if ((UWtype) au.s.low < (UWtype) bu.s.low)
		return 0;
	else if ((UWtype) au.s.low > (UWtype) bu.s.low)
		return 2;
	return 1;
}
