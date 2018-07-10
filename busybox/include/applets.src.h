/* vi: set sw=4 ts=4: */
/*
 * applets.h - a listing of all busybox applets.
 *
 * If you write a new applet, you need to add an entry to this list to make
 * busybox aware of it.
 */

/*
name  - applet name as it is typed on command line
help  - applet name, converted to C (ether-wake: help = ether_wake)
main  - corresponding <applet>_main to call (bzcat: main = bunzip2)
l     - location to install link to: [/usr]/[s]bin
s     - suid type:
        BB_SUID_REQUIRE: will complain if busybox isn't suid
        and is run by non-root (applet_main() will not be called at all)
        BB_SUID_DROP: will drop suid prior to applet_main()
        BB_SUID_MAYBE: neither of the above
        (every instance of BB_SUID_REQUIRE and BB_SUID_MAYBE
        needs to be justified in comment)
        NB: please update FEATURE_SUID help text whenever you add/remove
        BB_SUID_REQUIRE or BB_SUID_MAYBE applet.
*/

#if defined(PROTOTYPES)
# define APPLET(name,l,s)                    int name##_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
# define APPLET_ODDNAME(name,main,l,s,help)  int main##_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
# define APPLET_NOEXEC(name,main,l,s,help)   int main##_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
# define APPLET_NOFORK(name,main,l,s,help)   int main##_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;

#elif defined(NAME_MAIN)
# define APPLET(name,l,s)                    name name##_main
# define APPLET_ODDNAME(name,main,l,s,help)  name main##_main
# define APPLET_NOEXEC(name,main,l,s,help)   name main##_main
# define APPLET_NOFORK(name,main,l,s,help)   name main##_main

#elif defined(MAKE_USAGE) && ENABLE_FEATURE_VERBOSE_USAGE
# define APPLET(name,l,s)                    MAKE_USAGE(#name, name##_trivial_usage name##_full_usage)
# define APPLET_ODDNAME(name,main,l,s,help)  MAKE_USAGE(#name, help##_trivial_usage help##_full_usage)
# define APPLET_NOEXEC(name,main,l,s,help)   MAKE_USAGE(#name, help##_trivial_usage help##_full_usage)
# define APPLET_NOFORK(name,main,l,s,help)   MAKE_USAGE(#name, help##_trivial_usage help##_full_usage)

#elif defined(MAKE_USAGE) && !ENABLE_FEATURE_VERBOSE_USAGE
# define APPLET(name,l,s)                    MAKE_USAGE(#name, name##_trivial_usage)
# define APPLET_ODDNAME(name,main,l,s,help)  MAKE_USAGE(#name, help##_trivial_usage)
# define APPLET_NOEXEC(name,main,l,s,help)   MAKE_USAGE(#name, help##_trivial_usage)
# define APPLET_NOFORK(name,main,l,s,help)   MAKE_USAGE(#name, help##_trivial_usage)

#elif defined(MAKE_LINKS)
# define APPLET(name,l,c)                    LINK l name
# define APPLET_ODDNAME(name,main,l,s,help)  LINK l name
# define APPLET_NOEXEC(name,main,l,s,help)   LINK l name
# define APPLET_NOFORK(name,main,l,s,help)   LINK l name

#elif defined(MAKE_SUID)
# define APPLET(name,l,s)                    SUID s l name
# define APPLET_ODDNAME(name,main,l,s,help)  SUID s l name
# define APPLET_NOEXEC(name,main,l,s,help)   SUID s l name
# define APPLET_NOFORK(name,main,l,s,help)   SUID s l name

#else
  static struct bb_applet applets[] = { /*    name, main, location, need_suid */
# define APPLET(name,l,s)                    { #name, #name, l, s },
# define APPLET_ODDNAME(name,main,l,s,help)  { #name, #main, l, s },
# define APPLET_NOEXEC(name,main,l,s,help)   { #name, #main, l, s, 1 },
# define APPLET_NOFORK(name,main,l,s,help)   { #name, #main, l, s, 1, 1 },
#endif

#if ENABLE_INSTALL_NO_USR
# define BB_DIR_USR_BIN BB_DIR_BIN
# define BB_DIR_USR_SBIN BB_DIR_SBIN
#endif


INSERT


#if !defined(PROTOTYPES) && !defined(NAME_MAIN) && !defined(MAKE_USAGE) \
	&& !defined(MAKE_LINKS) && !defined(MAKE_SUID)
};
#endif

#undef APPLET
#undef APPLET_ODDNAME
#undef APPLET_NOEXEC
#undef APPLET_NOFORK
