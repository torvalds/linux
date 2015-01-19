#include <linux/cpufreq.h>

static struct cpufreq_frequency_table meson_freq_table[]=
{
//	0	, CPUFREQ_ENTRY_INVALID    ,
//	1	, CPUFREQ_ENTRY_INVALID    ,
	{0	, 96000    },
	{1	, 192000   },
	{2	, 312000   },
	{3	, 408000   },
	{4	, 504000   },
	{5	, 600000   },
	{6	, 696000   },
	{7	, 816000   },
	{8	, 912000   },
	{9	, 1008000  },
	{10	, 1104000  },
	{11	, 1200000  },
	{12	, 1296000  },
	{13	, 1416000  },
	{14	, 1512000  },
	{15	, 1608000  },
	{16	, 1800000  },
	{17	, 1992000  },
	{18	, CPUFREQ_TABLE_END},
};