define(MACHINE,powerpc64)dnl
vers(__file__,
	{-$OpenBSD: MAKEDEV.md,v 1.11 2022/01/07 01:13:15 jsg Exp $-},
etc.MACHINE)dnl
dnl
dnl Copyright (c) 2001-2006 Todd T. Fries <todd@OpenBSD.org>
dnl
dnl Permission to use, copy, modify, and distribute this software for any
dnl purpose with or without fee is hereby granted, provided that the above
dnl copyright notice and this permission notice appear in all copies.
dnl
dnl THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
dnl WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
dnl MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
dnl ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
dnl WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
dnl ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
dnl OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
dnl
dnl
__devitem(apm, apm, Power Management Interface)dnl
_TITLE(make)
_DEV(all)
_DEV(ramdisk)
_DEV(std)
_DEV(local)
_TITLE(dis)
_DEV(vnd, 24, 1)
_DEV(rd, 25, 2)
_DEV(sd, 26, 3)
_DEV(cd, 27, 4)
_DEV(wd, 17, 5)
_TITLE(tap)
_DEV(ch, 68)
_DEV(st, 69)
_TITLE(term)
_DEV(com, 57)
_TITLE(pty)
_DEV(ptm, 5)
_DEV(pty, 6)
_DEV(tty, 7)
_TITLE(cons)
_DEV(wscons)
_DEV(wsdisp, 40)
_DEV(wskbd, 41)
_DEV(wsmouse, 42)
_DEV(wsmux, 43)
_TITLE(prn)
_DEV(lpt, 64)
_TITLE(usb)
_DEV(ttyU, 58)
_DEV(uall)
_DEV(ugen, 49)
_DEV(uhid, 50)
_DEV(fido, 51)
_DEV(ujoy, 94)
_DEV(ulpt, 65)
_DEV(usb, 48)
_TITLE(spec)
_DEV(au, 32)
_DEV(bio, 80)
_DEV(bpf, 9)
_DEV(diskmap, 10)
_DEV(dri, 87)
_DEV(dt, 13)
_DEV(fdesc, 8)
_DEV(fuse, 81)
_DEV(hotplug, 82)
_DEV(ipmi, 88)
_DEV(kcov, 14)
_DEV(pci, 93)
_DEV(pf, 11)
_DEV(pppx, 72)
_DEV(pppac, 73)
_DEV(radio, 34)
_DEV(rnd, 12)
_DEV(rmidi, 33)
_DEV(tap, 75)
_DEV(tun, 76)
_DEV(uk, 70)
_DEV(vscsi, 83)
_DEV(kstat, 15)
dnl
divert(__mddivert)dnl
dnl
boot)
	_recurse ramdisk sd1 sd2 sd3 sd4 sd5 sd6 sd7 sd8 sd9
	M kexec	 	c 16 0 600
	;;
ramdisk)
	_recurse std bpf sd0 tty00 tty01 rd0 bio diskmap
	_recurse cd0 ttyC0 wskbd0 wskbd1 wskbd2 random
	;;

_std(1, 2, 3, 4)
	M openprom	c 92 0 600
	M opalcons0	c 56 0 600
	;;
dnl
dnl powerpc64 specific targets
dnl
twrget(all, au, audio, 0, 1, 2, 3)dnl
target(all, bio)dnl
target(all, cd, 0, 1)dnl
target(all, diskmap)dnl
target(all, dri)dnl
target(all, ipmi, 0)dnl
target(all, pty, 0)dnl
target(all, rd, 0)dnl
target(all, sd, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9)dnl
target(all, tap, 0, 1, 2, 3)dnl
target(all, tun, 0, 1, 2, 3)dnl
target(all, vnd, 0, 1, 2, 3)dnl
target(all, vscsi, 0)dnl
twrget(ramd, wsdisp, ttyC, 0)dnl
