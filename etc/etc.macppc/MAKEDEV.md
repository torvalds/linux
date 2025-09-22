define(MACHINE,macppc)dnl
vers(__file__,
	{-$OpenBSD: MAKEDEV.md,v 1.81 2022/11/09 19:35:23 krw Exp $-},
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
__devitem(s64_tzs, tty[a-z]*, Zilog 8530 serial ports,zs)dnl
__devitem(s64_czs, cua[a-z]*, Zilog 8530 serial ports,zs)dnl
_mkdev(s64_tzs, {-tty[a-z]-}, {-u=${i#tty*}
	case $u in
	a) n=0 ;;
	b) n=1 ;;
	*) echo unknown tty device $i ;;
	esac
	M tty$u c major_s64_tzs_c $n 660 dialer root-})dnl
_mkdev(s64_czs, cua[a-z], {-u=${i#cua*}
	case $u in
	a) n=0 ;;
	b) n=1 ;;
	*) echo unknown cua device $i ;;
	esac
	M cua$u c major_s64_czs_c Add($n, 128) 660 dialer root-})dnl
__devitem(apm, apm, Power management device)dnl
_TITLE(make)
_DEV(all)
_DEV(ramd)
_DEV(std)
_DEV(local)
_TITLE(dis)
_DEV(cd, 9, 3)
_DEV(rd, 17, 17)
_DEV(sd, 8, 2)
_DEV(vnd, 19, 14)
_DEV(wd, 11, 0)
_TITLE(tap)
_DEV(ch, 10)
_DEV(st, 20)
_TITLE(term)
_DEV(s64_czs, 7)
_DEV(com, 26)
_DEV(s64_tzs, 7)
_TITLE(pty)
_DEV(ptm, 77)
_DEV(pty, 5)
_DEV(tty, 4)
_TITLE(cons)
_DEV(wscons)
_DEV(wsdisp, 67)
_DEV(wskbd, 68)
_DEV(wsmux, 70)
_TITLE(point)
_DEV(wsmouse, 69)
_TITLE(usb)
_DEV(uall)
_DEV(ttyU, 66)
_DEV(ugen, 63)
_DEV(uhid, 62)
_DEV(fido, 90)
_DEV(ujoy, 92)
_DEV(ulpt, 64)
_DEV(usb, 61)
_TITLE(spec)
_DEV(apm, 25)
_DEV(au, 44)
_DEV(bio, 80)
_DEV(bktr, 75)
_DEV(bpf, 22)
_DEV(dt, 30)
_DEV(diskmap, 84)
_DEV(dri, 87)
_DEV(fdesc, 21)
_DEV(fuse, 88)
_DEV(gpio, 79)
_DEV(hotplug, 78)
_DEV(pci, 71)
_DEV(pf, 39)
_DEV(pppx, 85)
_DEV(pppac, 91)
_DEV(radio, 76)
_DEV(rnd, 40)
_DEV(rmidi, 52)
_DEV(tun, 23)
_DEV(tap, 86)
_DEV(tuner, 75)
_DEV(uk, 41)
_DEV(vi, 45)
_DEV(vscsi, 83)
_DEV(kstat, 51)
dnl
divert(__mddivert)dnl
dnl
_std(1, 2, 43, 6)
	M xf86		c 2 4 600
	M openprom	c 82 0 600
	;;

dnl
dnl *** macppc specific targets
dnl
twrget(all, au, audio, 0, 1, 2, 3)dnl
twrget(all, s64_tzs, tty, a, b)dnl
twrget(all, s64_czs, cua, a, b)dnl
target(all, ch, 0)dnl
target(all, vscsi, 0)dnl
target(all, diskmap)dnl
twrget(all, flo, fd, 0, 0B, 0C, 0D, 0E, 0F, 0G, 0H)dnl
twrget(all, flo, fd, 1, 1B, 1C, 1D, 1E, 1F, 1G, 1H)dnl
target(all, pty, 0)dnl
target(all, tun, 0, 1, 2, 3)dnl
target(all, tap, 0, 1, 2, 3)dnl
target(all, rd, 0)dnl
target(all, cd, 0, 1)dnl
target(all, sd, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9)dnl
target(all, vnd, 0, 1, 2, 3)dnl
target(all, gpio, 0, 1, 2)dnl
target(all, bio)dnl
target(all, dri)dnl
target(ramd, ttya, 0, 1)dnl
target(ramd, ttyb, 0, 1)dnl
target(ramd, bio)dnl
target(ramd, diskmap)dnl
target(ramd, random)dnl
