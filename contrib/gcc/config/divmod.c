long udivmodsi4 ();

long
__divsi3 (long a, long b)
{
  int neg = 0;
  long res;

  if (a < 0)
    {
      a = -a;
      neg = !neg;
    }

  if (b < 0)
    {
      b = -b;
      neg = !neg;
    }

  res = udivmodsi4 (a, b, 0);

  if (neg)
    res = -res;

  return res;
}

long
__modsi3 (long a, long b)
{
  int neg = 0;
  long res;

  if (a < 0)
    {
      a = -a;
      neg = 1;
    }

  if (b < 0)
    b = -b;

  res = udivmodsi4 (a, b, 1);

  if (neg)
    res = -res;

  return res;
}
