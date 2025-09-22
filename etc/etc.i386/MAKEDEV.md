define(MACHINE,i386)dnl
vers(__file__,
	{-$OpenBSD: MAKEDEV.md,v 1.96 2022/11/09 19:35:23 krw Exp $-},
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
__devitem(apm, apm, Power management device)dnl
__devitem(amdmsr, amdmsr, AMD MSR access device)dnl
__devitem(nvram, nvram, NVRAM access)dnl
_mkdev(nvram, nvram, {-M nvram c major_nvram_c 0 440 kmem-})dnl
_mkdev(amdmsr, amdmsr*, {-M amdmsr c major_amdmsr_c $U -})dnl
_TITLE(make)
_DEV(all)
_DEV(ramdisk)
_DEV(std)
_DEV(local)
_TITLE(dis)
_DEV(cd, 15, 6)
_DEV(flo, 9, 2)
_DEV(rd, 47, 17)
_DEV(sd, 13, 4)
_DEV(vnd, 41, 14)
_DEV(wd, 3, 0)
_TITLE(tap)
_DEV(ch, 17)
_DEV(st, 14)
_TITLE(term)
_DEV(com, 8)
_DEV(ttyc, 38)
_TITLE(pty)
_DEV(ptm, 81)
_DEV(pty, 6)
_DEV(tty, 5)
_TITLE(cons)
_DEV(wsdisp, 12)
_DEV(wscons)
_DEV(wskbd, 67)
_DEV(wsmux, 69)
_TITLE(point)
_DEV(wsmouse, 68)
_TITLE(prn)
_DEV(lpa)
_DEV(lpt, 16)
_TITLE(usb)
_DEV(ttyU, 66)
_DEV(uall)
_DEV(ugen, 63)
_DEV(uhid, 62)
_DEV(fido, 98)
_DEV(ujoy, 100)
_DEV(ulpt, 64)
_DEV(usb, 61)
_TITLE(spec)
_DEV(amdmsr, 89)
_DEV(apm, 21)
_DEV(au, 42)
_DEV(bio, 79)
_DEV(bktr, 49)
_DEV(bpf, 23)
_DEV(dt, 30)
_DEV(diskmap, 91)
_DEV(dri, 88)
_DEV(fdesc, 22)
_DEV(fuse, 93)
_DEV(gpio, 83)
_DEV(hotplug, 82)
_DEV(ipmi, 96)
_DEV(joy, 26)
_DEV(kcov, 19)
_DEV(nvram, 84)
_DEV(pci, 72)
_DEV(pctr, 46)
_DEV(pf, 73)
_DEV(pppx, 92)
_DEV(pppac, 99)
_DEV(radio, 76)
_DEV(rnd, 45)
_DEV(rmidi, 52)
_DEV(speak, 27)
_DEV(tun, 40)
_DEV(tap, 94)
_DEV(tuner, 49)
_DEV(uk, 20)
_DEV(vi, 44)
_DEV(vscsi, 90)
_DEV(pvbus, 95)
_DEV(kstat, 51)
dnl
divert(__mddivert)dnl
dnl
ramdisk)
	_recurse std bpf fd0 wd0 sd0 tty00 tty01 rd0 bio diskmap
	_recurse st0 cd0 ttyC0 wskbd0 wskbd1 wskbd2 apm random
	;;

_std(1, 2, 50, 7)
	M xf86		c 2 4 600
	;;

ttyc*)
	M ttyc$U c 38 $U 660 dialer root
	M cuac$U c 38 Add($U, 128) 660 dialer root
	;;
dnl
dnl i386 specific targets
dnl
twrget(all, au, audio, 0, 1, 2, 3)dnl
target(all, ch, 0)dnl
target(all, vscsi, 0)dnl
target(all, diskmap)dnl
twrget(all, flo, fd, 0, 0B, 0C, 0D, 0E, 0F, 0G, 0H)dnl
twrget(all, flo, fd, 1, 1B, 1C, 1D, 1E, 1F, 1G, 1H)dnl
target(all, pty, 0)dnl
target(all, bio)dnl
target(all, tun, 0, 1, 2, 3)dnl
target(all, tap, 0, 1, 2, 3)dnl
target(all, rd, 0)dnl
target(all, cd, 0, 1)dnl
target(all, sd, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9)dnl
target(all, vnd, 0, 1, 2, 3)dnl
target(all, bktr, 0)dnl
target(all, gpio, 0, 1, 2)dnl
target(all, nvram)dnl
target(all, dri)dnl
target(all, amdmsr)dnl
target(all, ipmi, 0)dnl
twrget(ramd, wsdisp, ttyC, 0)dnl
