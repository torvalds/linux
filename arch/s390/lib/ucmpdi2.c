#include <linux/module.h>

union ull_union {
	unsigned long long ull;
	struct {
		unsigned int high;
		unsigned int low;
	} ui;
};

int __ucmpdi2(unsigned long long a, unsigned long long b)
{
	union ull_union au = {.ull = a};
	union ull_union bu = {.ull = b};

	if (au.ui.high < bu.ui.high)
		return 0;
	else if (au.ui.high > bu.ui.high)
		return 2;
	if (au.ui.low < bu.ui.low)
		return 0;
	else if (au.ui.low > bu.ui.low)
		return 2;
	return 1;
}
EXPORT_SYMBOL(__ucmpdi2);
