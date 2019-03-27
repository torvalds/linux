/* Target signal numbers for GDB and the GDB remote protocol.
   Copyright 1986, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996,
   1997, 1998, 1999, 2000, 2001, 2002
   Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef GDB_SIGNALS_H
#define GDB_SIGNALS_H

/* The numbering of these signals is chosen to match traditional unix
   signals (insofar as various unices use the same numbers, anyway).
   It is also the numbering of the GDB remote protocol.  Other remote
   protocols, if they use a different numbering, should make sure to
   translate appropriately.

   Since these numbers have actually made it out into other software
   (stubs, etc.), you mustn't disturb the assigned numbering.  If you
   need to add new signals here, add them to the end of the explicitly
   numbered signals, at the comment marker.  Add them unconditionally,
   not within any #if or #ifdef.

   This is based strongly on Unix/POSIX signals for several reasons:
   (1) This set of signals represents a widely-accepted attempt to
   represent events of this sort in a portable fashion, (2) we want a
   signal to make it from wait to child_wait to the user intact, (3) many
   remote protocols use a similar encoding.  However, it is
   recognized that this set of signals has limitations (such as not
   distinguishing between various kinds of SIGSEGV, or not
   distinguishing hitting a breakpoint from finishing a single step).
   So in the future we may get around this either by adding additional
   signals for breakpoint, single-step, etc., or by adding signal
   codes; the latter seems more in the spirit of what BSD, System V,
   etc. are doing to address these issues.  */

/* For an explanation of what each signal means, see
   target_signal_to_string.  */

enum target_signal
  {
    /* Used some places (e.g. stop_signal) to record the concept that
       there is no signal.  */
    TARGET_SIGNAL_0 = 0,
    TARGET_SIGNAL_FIRST = 0,
    TARGET_SIGNAL_HUP = 1,
    TARGET_SIGNAL_INT = 2,
    TARGET_SIGNAL_QUIT = 3,
    TARGET_SIGNAL_ILL = 4,
    TARGET_SIGNAL_TRAP = 5,
    TARGET_SIGNAL_ABRT = 6,
    TARGET_SIGNAL_EMT = 7,
    TARGET_SIGNAL_FPE = 8,
    TARGET_SIGNAL_KILL = 9,
    TARGET_SIGNAL_BUS = 10,
    TARGET_SIGNAL_SEGV = 11,
    TARGET_SIGNAL_SYS = 12,
    TARGET_SIGNAL_PIPE = 13,
    TARGET_SIGNAL_ALRM = 14,
    TARGET_SIGNAL_TERM = 15,
    TARGET_SIGNAL_URG = 16,
    TARGET_SIGNAL_STOP = 17,
    TARGET_SIGNAL_TSTP = 18,
    TARGET_SIGNAL_CONT = 19,
    TARGET_SIGNAL_CHLD = 20,
    TARGET_SIGNAL_TTIN = 21,
    TARGET_SIGNAL_TTOU = 22,
    TARGET_SIGNAL_IO = 23,
    TARGET_SIGNAL_XCPU = 24,
    TARGET_SIGNAL_XFSZ = 25,
    TARGET_SIGNAL_VTALRM = 26,
    TARGET_SIGNAL_PROF = 27,
    TARGET_SIGNAL_WINCH = 28,
    TARGET_SIGNAL_LOST = 29,
    TARGET_SIGNAL_USR1 = 30,
    TARGET_SIGNAL_USR2 = 31,
    TARGET_SIGNAL_PWR = 32,
    /* Similar to SIGIO.  Perhaps they should have the same number.  */
    TARGET_SIGNAL_POLL = 33,
    TARGET_SIGNAL_WIND = 34,
    TARGET_SIGNAL_PHONE = 35,
    TARGET_SIGNAL_WAITING = 36,
    TARGET_SIGNAL_LWP = 37,
    TARGET_SIGNAL_DANGER = 38,
    TARGET_SIGNAL_GRANT = 39,
    TARGET_SIGNAL_RETRACT = 40,
    TARGET_SIGNAL_MSG = 41,
    TARGET_SIGNAL_SOUND = 42,
    TARGET_SIGNAL_SAK = 43,
    TARGET_SIGNAL_PRIO = 44,
    TARGET_SIGNAL_REALTIME_33 = 45,
    TARGET_SIGNAL_REALTIME_34 = 46,
    TARGET_SIGNAL_REALTIME_35 = 47,
    TARGET_SIGNAL_REALTIME_36 = 48,
    TARGET_SIGNAL_REALTIME_37 = 49,
    TARGET_SIGNAL_REALTIME_38 = 50,
    TARGET_SIGNAL_REALTIME_39 = 51,
    TARGET_SIGNAL_REALTIME_40 = 52,
    TARGET_SIGNAL_REALTIME_41 = 53,
    TARGET_SIGNAL_REALTIME_42 = 54,
    TARGET_SIGNAL_REALTIME_43 = 55,
    TARGET_SIGNAL_REALTIME_44 = 56,
    TARGET_SIGNAL_REALTIME_45 = 57,
    TARGET_SIGNAL_REALTIME_46 = 58,
    TARGET_SIGNAL_REALTIME_47 = 59,
    TARGET_SIGNAL_REALTIME_48 = 60,
    TARGET_SIGNAL_REALTIME_49 = 61,
    TARGET_SIGNAL_REALTIME_50 = 62,
    TARGET_SIGNAL_REALTIME_51 = 63,
    TARGET_SIGNAL_REALTIME_52 = 64,
    TARGET_SIGNAL_REALTIME_53 = 65,
    TARGET_SIGNAL_REALTIME_54 = 66,
    TARGET_SIGNAL_REALTIME_55 = 67,
    TARGET_SIGNAL_REALTIME_56 = 68,
    TARGET_SIGNAL_REALTIME_57 = 69,
    TARGET_SIGNAL_REALTIME_58 = 70,
    TARGET_SIGNAL_REALTIME_59 = 71,
    TARGET_SIGNAL_REALTIME_60 = 72,
    TARGET_SIGNAL_REALTIME_61 = 73,
    TARGET_SIGNAL_REALTIME_62 = 74,
    TARGET_SIGNAL_REALTIME_63 = 75,

    /* Used internally by Solaris threads.  See signal(5) on Solaris.  */
    TARGET_SIGNAL_CANCEL = 76,

    /* Yes, this pains me, too.  But LynxOS didn't have SIG32, and now
       GNU/Linux does, and we can't disturb the numbering, since it's
       part of the remote protocol.  Note that in some GDB's
       TARGET_SIGNAL_REALTIME_32 is number 76.  */
    TARGET_SIGNAL_REALTIME_32,
    /* Yet another pain, IRIX 6 has SIG64. */
    TARGET_SIGNAL_REALTIME_64,
    /* Yet another pain, GNU/Linux MIPS might go up to 128. */
    TARGET_SIGNAL_REALTIME_65,
    TARGET_SIGNAL_REALTIME_66,
    TARGET_SIGNAL_REALTIME_67,
    TARGET_SIGNAL_REALTIME_68,
    TARGET_SIGNAL_REALTIME_69,
    TARGET_SIGNAL_REALTIME_70,
    TARGET_SIGNAL_REALTIME_71,
    TARGET_SIGNAL_REALTIME_72,
    TARGET_SIGNAL_REALTIME_73,
    TARGET_SIGNAL_REALTIME_74,
    TARGET_SIGNAL_REALTIME_75,
    TARGET_SIGNAL_REALTIME_76,
    TARGET_SIGNAL_REALTIME_77,
    TARGET_SIGNAL_REALTIME_78,
    TARGET_SIGNAL_REALTIME_79,
    TARGET_SIGNAL_REALTIME_80,
    TARGET_SIGNAL_REALTIME_81,
    TARGET_SIGNAL_REALTIME_82,
    TARGET_SIGNAL_REALTIME_83,
    TARGET_SIGNAL_REALTIME_84,
    TARGET_SIGNAL_REALTIME_85,
    TARGET_SIGNAL_REALTIME_86,
    TARGET_SIGNAL_REALTIME_87,
    TARGET_SIGNAL_REALTIME_88,
    TARGET_SIGNAL_REALTIME_89,
    TARGET_SIGNAL_REALTIME_90,
    TARGET_SIGNAL_REALTIME_91,
    TARGET_SIGNAL_REALTIME_92,
    TARGET_SIGNAL_REALTIME_93,
    TARGET_SIGNAL_REALTIME_94,
    TARGET_SIGNAL_REALTIME_95,
    TARGET_SIGNAL_REALTIME_96,
    TARGET_SIGNAL_REALTIME_97,
    TARGET_SIGNAL_REALTIME_98,
    TARGET_SIGNAL_REALTIME_99,
    TARGET_SIGNAL_REALTIME_100,
    TARGET_SIGNAL_REALTIME_101,
    TARGET_SIGNAL_REALTIME_102,
    TARGET_SIGNAL_REALTIME_103,
    TARGET_SIGNAL_REALTIME_104,
    TARGET_SIGNAL_REALTIME_105,
    TARGET_SIGNAL_REALTIME_106,
    TARGET_SIGNAL_REALTIME_107,
    TARGET_SIGNAL_REALTIME_108,
    TARGET_SIGNAL_REALTIME_109,
    TARGET_SIGNAL_REALTIME_110,
    TARGET_SIGNAL_REALTIME_111,
    TARGET_SIGNAL_REALTIME_112,
    TARGET_SIGNAL_REALTIME_113,
    TARGET_SIGNAL_REALTIME_114,
    TARGET_SIGNAL_REALTIME_115,
    TARGET_SIGNAL_REALTIME_116,
    TARGET_SIGNAL_REALTIME_117,
    TARGET_SIGNAL_REALTIME_118,
    TARGET_SIGNAL_REALTIME_119,
    TARGET_SIGNAL_REALTIME_120,
    TARGET_SIGNAL_REALTIME_121,
    TARGET_SIGNAL_REALTIME_122,
    TARGET_SIGNAL_REALTIME_123,
    TARGET_SIGNAL_REALTIME_124,
    TARGET_SIGNAL_REALTIME_125,
    TARGET_SIGNAL_REALTIME_126,
    TARGET_SIGNAL_REALTIME_127,

    TARGET_SIGNAL_INFO,

    /* Some signal we don't know about.  */
    TARGET_SIGNAL_UNKNOWN,

    /* Use whatever signal we use when one is not specifically specified
       (for passing to proceed and so on).  */
    TARGET_SIGNAL_DEFAULT,

    /* Mach exceptions.  In versions of GDB before 5.2, these were just before
       TARGET_SIGNAL_INFO if you were compiling on a Mach host (and missing
       otherwise).  */
    TARGET_EXC_BAD_ACCESS,
    TARGET_EXC_BAD_INSTRUCTION,
    TARGET_EXC_ARITHMETIC,
    TARGET_EXC_EMULATION,
    TARGET_EXC_SOFTWARE,
    TARGET_EXC_BREAKPOINT,

    /* If you are adding a new signal, add it just above this comment.  */

    /* Last and unused enum value, for sizing arrays, etc.  */
    TARGET_SIGNAL_LAST
  };

#endif /* #ifndef GDB_SIGNALS_H */
