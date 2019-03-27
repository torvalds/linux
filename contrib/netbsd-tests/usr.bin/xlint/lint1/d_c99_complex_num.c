double cabs(double _Complex);

double cabs(double _Complex foo)
{
	double d = __real__ foo;
	return d + 0.1fi;
}
