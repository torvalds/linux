int num_digits(int val)
{
	int digits = 0;

	while (val) {
		val /= 10;
		digits++;
	}

	return digits;
}
