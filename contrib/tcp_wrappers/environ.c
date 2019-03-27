/*
 * Many systems have putenv() but no setenv(). Other systems have setenv()
 * but no putenv() (MIPS). Still other systems have neither (NeXT). This is a
 * re-implementation that hopefully ends all problems.
 *
 * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
 */

#ifndef lint
static char sccsid[] = "@(#) environ.c 1.2 94/03/23 16:09:46";
#endif

/* System libraries. */

extern char **environ;
extern char *strchr();
extern char *strcpy();
extern char *strncpy();
extern char *malloc();
extern char *realloc();
extern int strncmp();
extern void free();

#ifdef no_memcpy
#define memcpy(d,s,l) bcopy(s,d,l)
#else
extern char *memcpy();
#endif

/* Local stuff. */

static int addenv();			/* append entry to environment */

static int allocated = 0;		/* environ is, or is not, allocated */

#define DO_CLOBBER	1

/* namelength - determine length of name in "name=whatever" */

static int namelength(name)
char   *name;
{
    char   *equal;

    equal = strchr(name, '=');
    return ((equal == 0) ? strlen(name) : (equal - name));
}

/* findenv - given name, locate name=value */

static char **findenv(name, len)
char   *name;
int     len;
{
    char  **envp;

    for (envp = environ; envp && *envp; envp++)
	if (strncmp(name, *envp, len) == 0 && (*envp)[len] == '=')
	    return (envp);
    return (0);
}

/* getenv - given name, locate value */

char   *getenv(name)
char   *name;
{
    int     len = namelength(name);
    char  **envp = findenv(name, len);

    return (envp ? *envp + len + 1 : 0);
}

/* putenv - update or append environment (name,value) pair */

int     putenv(nameval)
char   *nameval;
{
    char   *equal = strchr(nameval, '=');
    char   *value = (equal ? equal : "");

    return (setenv(nameval, value, DO_CLOBBER));
}

/* unsetenv - remove variable from environment */

void    unsetenv(name)
char   *name;
{
    char  **envp;

    if ((envp = findenv(name, namelength(name))) != 0)
	while (envp[0] = envp[1])
	    envp++;
}

/* setenv - update or append environment (name,value) pair */

int     setenv(name, value, clobber)
char   *name;
char   *value;
int     clobber;
{
    char   *destination;
    char  **envp;
    int     l_name;			/* length of name part */
    int     l_nameval;			/* length of name=value */

    /* Permit name= and =value. */

    l_name = namelength(name);
    envp = findenv(name, l_name);
    if (envp != 0 && clobber == 0)
	return (0);
    if (*value == '=')
	value++;
    l_nameval = l_name + strlen(value) + 1;

    /*
     * Use available memory if the old value is long enough. Never free an
     * old name=value entry because it may not be allocated.
     */

    destination = (envp != 0 && strlen(*envp) >= l_nameval) ?
	*envp : malloc(l_nameval + 1);
    if (destination == 0)
	return (-1);
    strncpy(destination, name, l_name);
    destination[l_name] = '=';
    strcpy(destination + l_name + 1, value);
    return ((envp == 0) ? addenv(destination) : (*envp = destination, 0));
}

/* cmalloc - malloc and copy block of memory */

static char *cmalloc(new_len, old, old_len)
char   *old;
int     old_len;
{
    char   *new = malloc(new_len);

    if (new != 0)
	memcpy(new, old, old_len);
    return (new);
}

/* addenv - append environment entry */

static int addenv(nameval)
char   *nameval;
{
    char  **envp;
    int     n_used;			/* number of environment entries */
    int     l_used;			/* bytes used excl. terminator */
    int     l_need;			/* bytes needed incl. terminator */

    for (envp = environ; envp && *envp; envp++)
	 /* void */ ;
    n_used = envp - environ;
    l_used = n_used * sizeof(*envp);
    l_need = l_used + 2 * sizeof(*envp);

    envp = allocated ?
	(char **) realloc((char *) environ, l_need) :
	(char **) cmalloc(l_need, (char *) environ, l_used);
    if (envp == 0) {
	return (-1);
    } else {
	allocated = 1;
	environ = envp;
	environ[n_used++] = nameval;		/* add new entry */
	environ[n_used] = 0;			/* terminate list */
	return (0);
    }
}

#ifdef TEST

 /*
  * Stand-alone program for test purposes.
  */

/* printenv - display environment */

static void printenv()
{
    char  **envp;

    for (envp = environ; envp && *envp; envp++)
	printf("%s\n", *envp);
}

int     main(argc, argv)
int     argc;
char  **argv;
{
    char   *cp;
    int     changed = 0;

    if (argc < 2) {
	printf("usage: %s name[=value]...\n", argv[0]);
	return (1);
    }
    while (--argc && *++argv) {
	if (argv[0][0] == '-') {		/* unsetenv() test */
	    unsetenv(argv[0] + 1);
	    changed = 1;
	} else if (strchr(argv[0], '=') == 0) {	/* getenv() test */
	    cp = getenv(argv[0]);
	    printf("%s: %s\n", argv[0], cp ? cp : "not found");
	} else {				/* putenv() test */
	    if (putenv(argv[0])) {
		perror("putenv");
		return (1);
	    }
	    changed = 1;
	}
    }
    if (changed)
	printenv();
    return (0);
}

#endif /* TEST */
