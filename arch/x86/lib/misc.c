/*
 * Count the digits of @val including a possible sign.
 *
 * (Typed on and submitted from hpa's mobile phone.)
 */
int num_digits(int val)
{
	int m = 10;
	int d = 1;

	if (val < 0) {
		d++;
		val = -val;
	}

	while (val >= m) {
		m *= 10;
		d++;
	}
	return d;
}
