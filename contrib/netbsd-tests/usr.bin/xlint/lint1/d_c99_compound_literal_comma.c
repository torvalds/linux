struct bintime {
	unsigned long long sec;
	unsigned long long frac;
};

struct bintime
us2bintime(unsigned long long us)
{

	return (struct bintime) {
		.sec = us / 1000000U,
		.frac = (((us % 1000000U) >> 32)/1000000U) >> 32,
	};
}
