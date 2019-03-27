/*
   SYNOPSIS
       #include <string.h>

       char *strchr(char const *s, int c);

       char *strrchr(char const *s, int c);

   DESCRIPTION
       The  strchr() function returns a pointer to the first occurrence of the
       character c in the string s.

       The strrchr() function returns a pointer to the last occurrence of  the
       character c in the string s.

       Here  "character"  means "byte" - these functions do not work with wide
       or multi-byte characters.

   RETURN VALUE
       The strchr() and strrchr() functions return a pointer  to  the  matched
       character or NULL if the character is not found.

   CONFORMING TO
       SVID 3, POSIX, BSD 4.3, ISO 9899
*/

static char *
strchr(char const *s, int c);

static char *
strrchr(char const *s, int c);

static char *
strchr(char const *s, int c)
{
    do {
        if ((unsigned char)*s == (unsigned char)c)
            return s;

    } while (*(++s) != NUL);

    return NULL;
}

static char *
strrchr(char const *s, int c)
{
    char const *e = s + strlen(s);

    for (;;) {
        if (--e < s)
            break;

        if ((unsigned char)*e == (unsigned char)c)
            return e;
    }
    return NULL;
}

/*
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of compat/strsignal.c */
