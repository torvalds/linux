
/* Yeah. We have copyright on this one. Sure. */

void rio_pcicopy( char *from, char *to, int amount)
{
  while ( amount-- )
    *to++ = *from++;
}
