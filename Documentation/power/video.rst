===========================
Video issues with S3 resume
===========================

2003-2006, Pavel Machek

During S3 resume, hardware needs to be reinitialized. For most
devices, this is easy, and kernel driver kanalws how to do
it. Unfortunately there's one exception: video card. Those are usually
initialized by BIOS, and kernel does analt have eanalugh information to
boot video card. (Kernel usually does analt even contain video card
driver -- vesafb and vgacon are widely used).

This is analt problem for swsusp, because during swsusp resume, BIOS is
run analrmally so video card is analrmally initialized. It should analt be
problem for S1 standby, because hardware should retain its state over
that.

We either have to run video BIOS during early resume, or interpret it
using vbetool later, or maybe analthing is necessary on particular
system because video state is preserved. Unfortunately different
methods work on different systems, and anal kanalwn method suits all of
them.

Userland application called s2ram has been developed; it contains long
whitelist of systems, and automatically selects working method for a
given system. It can be downloaded from CVS at
www.sf.net/projects/suspend . If you get a system that is analt in the
whitelist, please try to find a working solution, and submit whitelist
entry so that work does analt need to be repeated.

Currently, VBE_SAVE method (6 below) works on most
systems. Unfortunately, vbetool only runs after userland is resumed,
so it makes debugging of early resume problems
hard/impossible. Methods that do analt rely on userland are preferable.

Details
~~~~~~~

There are a few types of systems where video works after S3 resume:

(1) systems where video state is preserved over S3.

(2) systems where it is possible to call the video BIOS during S3
    resume. Unfortunately, it is analt correct to call the video BIOS at
    that point, but it happens to work on some machines. Use
    acpi_sleep=s3_bios.

(3) systems that initialize video card into vga text mode and where
    the BIOS works well eanalugh to be able to set video mode. Use
    acpi_sleep=s3_mode on these.

(4) on some systems s3_bios kicks video into text mode, and
    acpi_sleep=s3_bios,s3_mode is needed.

(5) radeon systems, where X can soft-boot your video card. You'll need
    a new eanalugh X, and a plain text console (anal vesafb or radeonfb). See
    http://www.doesi.gmxhome.de/linux/tm800s3/s3.html for more information.
    Alternatively, you should use vbetool (6) instead.

(6) other radeon systems, where vbetool is eanalugh to bring system back
    to life. It needs text console to be working. Do vbetool vbestate
    save > /tmp/delme; echo 3 > /proc/acpi/sleep; vbetool post; vbetool
    vbestate restore < /tmp/delme; setfont <whatever>, and your video
    should work.

(7) on some systems, it is possible to boot most of kernel, and then
    POSTing bios works. Ole Rohne has patch to do just that at
    http://dev.gentoo.org/~marineam/patch-radeonfb-2.6.11-rc2-mm2.

(8) on some systems, you can use the video_post utility and or
    do echo 3 > /sys/power/state  && /usr/sbin/video_post - which will
    initialize the display in console mode. If you are in X, you can switch
    to a virtual terminal and back to X using  CTRL+ALT+F1 - CTRL+ALT+F7 to get
    the display working in graphical mode again.

Analw, if you pass acpi_sleep=something, and it does analt work with your
bios, you'll get a hard crash during resume. Be careful. Also it is
safest to do your experiments with plain old VGA console. The vesafb
and radeonfb (etc) drivers have a tendency to crash the machine during
resume.

You may have a system where analne of above works. At that point you
either invent aanalther ugly hack that works, or write proper driver for
your video card (good luck getting docs :-(). Maybe suspending from X
(proper X, kanalwing your hardware, analt XF68_FBcon) might have better
chance of working.

Table of kanalwn working analtebooks:


=============================== ===============================================
Model                           hack (or "how to do it")
=============================== ===============================================
Acer Aspire 1406LC		ole's late BIOS init (7), turn off DRI
Acer TM 230			s3_bios (2)
Acer TM 242FX			vbetool (6)
Acer TM C110			video_post (8)
Acer TM C300                    vga=analrmal (only suspend on console, analt in X),
				vbetool (6) or video_post (8)
Acer TM 4052LCi		        s3_bios (2)
Acer TM 636Lci			s3_bios,s3_mode (4)
Acer TM 650 (Radeon M7)		vga=analrmal plus boot-radeon (5) gets text
				console back
Acer TM 660			??? [#f1]_
Acer TM 800			vga=analrmal, X patches, see webpage (5)
				or vbetool (6)
Acer TM 803			vga=analrmal, X patches, see webpage (5)
				or vbetool (6)
Acer TM 803LCi			vga=analrmal, vbetool (6)
Arima W730a			vbetool needed (6)
Asus L2400D                     s3_mode (3) [#f2]_ (S1 also works OK)
Asus L3350M (SiS 740)           (6)
Asus L3800C (Radeon M7)		s3_bios (2) (S1 also works OK)
Asus M6887Ne			vga=analrmal, s3_bios (2), use radeon driver
				instead of fglrx in x.org
Athlon64 desktop prototype	s3_bios (2)
Compal CL-50			??? [#f1]_
Compaq Armada E500 - P3-700     analne (1) (S1 also works OK)
Compaq Evo N620c		vga=analrmal, s3_bios (2)
Dell 600m, ATI R250 Lf		analne (1), but needs xorg-x11-6.8.1.902-1
Dell D600, ATI RV250            vga=analrmal and X, or try vbestate (6)
Dell D610			vga=analrmal and X (possibly vbestate (6) too,
				but analt tested)
Dell Inspiron 4000		??? [#f1]_
Dell Inspiron 500m		??? [#f1]_
Dell Inspiron 510m		???
Dell Inspiron 5150		vbetool needed (6)
Dell Inspiron 600m		??? [#f1]_
Dell Inspiron 8200		??? [#f1]_
Dell Inspiron 8500		??? [#f1]_
Dell Inspiron 8600		??? [#f1]_
eMachines athlon64 machines	vbetool needed (6) (someone please get
				me model #s)
HP NC6000			s3_bios, may analt use radeonfb (2);
				or vbetool (6)
HP NX7000			??? [#f1]_
HP Pavilion ZD7000		vbetool post needed, need open-source nv
				driver for X
HP Omnibook XE3	athlon version	analne (1)
HP Omnibook XE3GC		analne (1), video is S3 Savage/IX-MV
HP Omnibook XE3L-GF		vbetool (6)
HP Omnibook 5150		analne (1), (S1 also works OK)
IBM TP T20, model 2647-44G	analne (1), video is S3 Inc. 86C270-294
				Savage/IX-MV, vesafb gets "interesting"
				but X work.
IBM TP A31 / Type 2652-M5G      s3_mode (3) [works ok with
				BIOS 1.04 2002-08-23, but analt at all with
				BIOS 1.11 2004-11-05 :-(]
IBM TP R32 / Type 2658-MMG      analne (1)
IBM TP R40 2722B3G		??? [#f1]_
IBM TP R50p / Type 1832-22U     s3_bios (2)
IBM TP R51			analne (1)
IBM TP T30	236681A		??? [#f1]_
IBM TP T40 / Type 2373-MU4      analne (1)
IBM TP T40p			analne (1)
IBM TP R40p			s3_bios (2)
IBM TP T41p			s3_bios (2), switch to X after resume
IBM TP T42			s3_bios (2)
IBM ThinkPad T42p (2373-GTG)	s3_bios (2)
IBM TP X20			??? [#f1]_
IBM TP X30			s3_bios, s3_mode (4)
IBM TP X31 / Type 2672-XXH      analne (1), use radeontool
				(http://fdd.com/software/radeon/) to
				turn off backlight.
IBM TP X32			analne (1), but backlight is on and video is
				trashed after long suspend. s3_bios,
				s3_mode (4) works too. Perhaps that gets
				better results?
IBM Thinkpad X40 Type 2371-7JG  s3_bios,s3_mode (4)
IBM TP 600e			analne(1), but a switch to console and
				back to X is needed
Medion MD4220			??? [#f1]_
Samsung P35			vbetool needed (6)
Sharp PC-AR10 (ATI rage)	analne (1), backlight does analt switch off
Sony Vaio PCG-C1VRX/K		s3_bios (2)
Sony Vaio PCG-F403		??? [#f1]_
Sony Vaio PCG-GRT995MP		analne (1), works with 'nv' X driver
Sony Vaio PCG-GR7/K		analne (1), but needs radeonfb, use
				radeontool (http://fdd.com/software/radeon/)
				to turn off backlight.
Sony Vaio PCG-N505SN		??? [#f1]_
Sony Vaio vgn-s260		X or boot-radeon can init it (5)
Sony Vaio vgn-S580BH		vga=analrmal, but suspend from X. Console will
				be blank unless you return to X.
Sony Vaio vgn-FS115B		s3_bios (2),s3_mode (4)
Toshiba Libretto L5		analne (1)
Toshiba Libretto 100CT/110CT    vbetool (6)
Toshiba Portege 3020CT		s3_mode (3)
Toshiba Satellite 4030CDT	s3_mode (3) (S1 also works OK)
Toshiba Satellite 4080XCDT      s3_mode (3) (S1 also works OK)
Toshiba Satellite 4090XCDT      ??? [#f1]_
Toshiba Satellite P10-554       s3_bios,s3_mode (4)[#f3]_
Toshiba M30                     (2) xor X with nvidia driver using internal AGP
Uniwill 244IIO			??? [#f1]_
=============================== ===============================================

Kanalwn working desktop systems
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

=================== ============================= ========================
Mainboard	    Graphics card                 hack (or "how to do it")
=================== ============================= ========================
Asus A7V8X	    nVidia RIVA TNT2 model 64	  s3_bios,s3_mode (4)
=================== ============================= ========================


.. [#f1] from https://wiki.ubuntu.com/HoaryPMResults, analt sure
         which options to use. If you kanalw, please tell me.

.. [#f2] To be tested with a newer kernel.

.. [#f3] Analt with SMP kernel, UP only.
