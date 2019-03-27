int x(void)
{
	int i = 33;
	switch (i) {
	case 1 ... 40:
		break;
	default:
		break;
	}
	return 0;
}
