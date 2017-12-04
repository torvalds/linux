==============================
HD-Audio Codec-Specific Models
==============================

ALC880
======
3stack
    3-jack in back and a headphone out
3stack-digout
    3-jack in back, a HP out and a SPDIF out
5stack
    5-jack in back, 2-jack in front
5stack-digout
    5-jack in back, 2-jack in front, a SPDIF out
6stack
    6-jack in back, 2-jack in front
6stack-digout
    6-jack with a SPDIF out
6stack-automute
    6-jack with headphone jack detection

ALC260
======
gpio1
    Enable GPIO1
coef
    Enable EAPD via COEF table
fujitsu
    Quirk for FSC S7020
fujitsu-jwse
    Quirk for FSC S7020 with jack modes and HP mic support

ALC262
======
inv-dmic
    Inverted internal mic workaround

ALC267/268
==========
inv-dmic
    Inverted internal mic workaround
hp-eapd
    Disable HP EAPD on NID 0x15

ALC22x/23x/25x/269/27x/28x/29x (and vendor-specific ALC3xxx models)
===================================================================
laptop-amic
    Laptops with analog-mic input
laptop-dmic
    Laptops with digital-mic input
alc269-dmic
    Enable ALC269(VA) digital mic workaround
alc271-dmic
    Enable ALC271X digital mic workaround
inv-dmic
    Inverted internal mic workaround
headset-mic
    Indicates a combined headset (headphone+mic) jack
headset-mode
    More comprehensive headset support for ALC269 & co
headset-mode-no-hp-mic
    Headset mode support without headphone mic
lenovo-dock
    Enables docking station I/O for some Lenovos
hp-gpio-led
    GPIO LED support on HP laptops
hp-dock-gpio-mic1-led
    HP dock with mic LED support
dell-headset-multi
    Headset jack, which can also be used as mic-in
dell-headset-dock
    Headset jack (without mic-in), and also dock I/O
alc283-dac-wcaps
    Fixups for Chromebook with ALC283
alc283-sense-combo
    Combo jack sensing on ALC283
tpt440-dock
    Pin configs for Lenovo Thinkpad Dock support
tpt440
    Lenovo Thinkpad T440s setup
tpt460
    Lenovo Thinkpad T460/560 setup
dual-codecs
    Lenovo laptops with dual codecs
alc700-ref
    Intel reference board with ALC700 codec

ALC66x/67x/892
==============
mario
    Chromebook mario model fixup
asus-mode1
    ASUS
asus-mode2
    ASUS
asus-mode3
    ASUS
asus-mode4
    ASUS
asus-mode5
    ASUS
asus-mode6
    ASUS
asus-mode7
    ASUS
asus-mode8
    ASUS
inv-dmic
    Inverted internal mic workaround
dell-headset-multi
    Headset jack, which can also be used as mic-in
dual-codecs
    Lenovo laptops with dual codecs

ALC680
======
N/A

ALC88x/898/1150
======================
acer-aspire-4930g
    Acer Aspire 4930G/5930G/6530G/6930G/7730G
acer-aspire-8930g
    Acer Aspire 8330G/6935G
acer-aspire
    Acer Aspire others
inv-dmic
    Inverted internal mic workaround
no-primary-hp
    VAIO Z/VGC-LN51JGB workaround (for fixed speaker DAC)
dual-codecs
    ALC1220 dual codecs for Gaming mobos

ALC861/660
==========
N/A

ALC861VD/660VD
==============
N/A

CMI9880
=======
minimal
    3-jack in back
min_fp
    3-jack in back, 2-jack in front
full
    6-jack in back, 2-jack in front
full_dig
    6-jack in back, 2-jack in front, SPDIF I/O
allout
    5-jack in back, 2-jack in front, SPDIF out
auto
    auto-config reading BIOS (default)

AD1882 / AD1882A
================
3stack
    3-stack mode
3stack-automute
    3-stack with automute front HP (default)
6stack
    6-stack mode

AD1884A / AD1883 / AD1984A / AD1984B
====================================
desktop	3-stack desktop (default)
laptop	laptop with HP jack sensing
mobile	mobile devices with HP jack sensing
thinkpad	Lenovo Thinkpad X300
touchsmart	HP Touchsmart

AD1884
======
N/A

AD1981
======
basic		3-jack (default)
hp		HP nx6320
thinkpad	Lenovo Thinkpad T60/X60/Z60
toshiba	Toshiba U205

AD1983
======
N/A

AD1984
======
basic		default configuration
thinkpad	Lenovo Thinkpad T61/X61
dell_desktop	Dell T3400

AD1986A
=======
3stack
    3-stack, shared surrounds
laptop
    2-channel only (FSC V2060, Samsung M50)
laptop-imic
    2-channel with built-in mic
eapd
    Turn on EAPD constantly

AD1988/AD1988B/AD1989A/AD1989B
==============================
6stack
    6-jack
6stack-dig
    ditto with SPDIF
3stack
    3-jack
3stack-dig
    ditto with SPDIF
laptop
    3-jack with hp-jack automute
laptop-dig
    ditto with SPDIF
auto
    auto-config reading BIOS (default)

Conexant 5045
=============
cap-mix-amp
    Fix max input level on mixer widget
toshiba-p105
    Toshiba P105 quirk
hp-530
    HP 530 quirk

Conexant 5047
=============
cap-mix-amp
    Fix max input level on mixer widget

Conexant 5051
=============
lenovo-x200
    Lenovo X200 quirk

Conexant 5066
=============
stereo-dmic
    Workaround for inverted stereo digital mic
gpio1
    Enable GPIO1 pin
headphone-mic-pin
    Enable headphone mic NID 0x18 without detection
tp410
    Thinkpad T400 & co quirks
thinkpad
    Thinkpad mute/mic LED quirk
lemote-a1004
    Lemote A1004 quirk
lemote-a1205
    Lemote A1205 quirk
olpc-xo
    OLPC XO quirk
mute-led-eapd
    Mute LED control via EAPD
hp-dock
    HP dock support
mute-led-gpio
    Mute LED control via GPIO

STAC9200
========
ref
    Reference board
oqo
    OQO Model 2
dell-d21
    Dell (unknown)
dell-d22
    Dell (unknown)
dell-d23
    Dell (unknown)
dell-m21
    Dell Inspiron 630m, Dell Inspiron 640m
dell-m22
    Dell Latitude D620, Dell Latitude D820
dell-m23
    Dell XPS M1710, Dell Precision M90
dell-m24
    Dell Latitude 120L
dell-m25
    Dell Inspiron E1505n
dell-m26
    Dell Inspiron 1501
dell-m27
    Dell Inspiron E1705/9400
gateway-m4
    Gateway laptops with EAPD control
gateway-m4-2
    Gateway laptops with EAPD control
panasonic
    Panasonic CF-74
auto
    BIOS setup (default)

STAC9205/9254
=============
ref
    Reference board
dell-m42
    Dell (unknown)
dell-m43
    Dell Precision
dell-m44
    Dell Inspiron
eapd
    Keep EAPD on (e.g. Gateway T1616)
auto
    BIOS setup (default)

STAC9220/9221
=============
ref
    Reference board
3stack
    D945 3stack
5stack
    D945 5stack + SPDIF
intel-mac-v1
    Intel Mac Type 1
intel-mac-v2
    Intel Mac Type 2
intel-mac-v3
    Intel Mac Type 3
intel-mac-v4
    Intel Mac Type 4
intel-mac-v5
    Intel Mac Type 5
intel-mac-auto
    Intel Mac (detect type according to subsystem id)
macmini
    Intel Mac Mini (equivalent with type 3)
macbook
    Intel Mac Book (eq. type 5)
macbook-pro-v1
    Intel Mac Book Pro 1st generation (eq. type 3)
macbook-pro
    Intel Mac Book Pro 2nd generation (eq. type 3)
imac-intel
    Intel iMac (eq. type 2)
imac-intel-20
    Intel iMac (newer version) (eq. type 3)
ecs202
    ECS/PC chips
dell-d81
    Dell (unknown)
dell-d82
    Dell (unknown)
dell-m81
    Dell (unknown)
dell-m82
    Dell XPS M1210
auto
    BIOS setup (default)

STAC9202/9250/9251
==================
ref
    Reference board, base config
m1
    Some Gateway MX series laptops (NX560XL)
m1-2
    Some Gateway MX series laptops (MX6453)
m2
    Some Gateway MX series laptops (M255)
m2-2
    Some Gateway MX series laptops
m3
    Some Gateway MX series laptops
m5
    Some Gateway MX series laptops (MP6954)
m6
    Some Gateway NX series laptops
auto
    BIOS setup (default)

STAC9227/9228/9229/927x
=======================
ref
    Reference board
ref-no-jd
    Reference board without HP/Mic jack detection
3stack
    D965 3stack
5stack
    D965 5stack + SPDIF
5stack-no-fp
    D965 5stack without front panel
dell-3stack
    Dell Dimension E520
dell-bios
    Fixes with Dell BIOS setup
dell-bios-amic
    Fixes with Dell BIOS setup including analog mic
volknob
    Fixes with volume-knob widget 0x24
auto
    BIOS setup (default)

STAC92HD71B*
============
ref
    Reference board
dell-m4-1
    Dell desktops
dell-m4-2
    Dell desktops
dell-m4-3
    Dell desktops
hp-m4
    HP mini 1000
hp-dv5
    HP dv series
hp-hdx
    HP HDX series
hp-dv4-1222nr
    HP dv4-1222nr (with LED support)
auto
    BIOS setup (default)

STAC92HD73*
===========
ref
    Reference board
no-jd
    BIOS setup but without jack-detection
intel
    Intel DG45* mobos
dell-m6-amic
    Dell desktops/laptops with analog mics
dell-m6-dmic
    Dell desktops/laptops with digital mics
dell-m6
    Dell desktops/laptops with both type of mics
dell-eq
    Dell desktops/laptops
alienware
    Alienware M17x
asus-mobo
    Pin configs for ASUS mobo with 5.1/SPDIF out
auto
    BIOS setup (default)

STAC92HD83*
===========
ref
    Reference board
mic-ref
    Reference board with power management for ports
dell-s14
    Dell laptop
dell-vostro-3500
    Dell Vostro 3500 laptop
hp-dv7-4000
    HP dv-7 4000
hp_cNB11_intquad
    HP CNB models with 4 speakers
hp-zephyr
    HP Zephyr
hp-led
    HP with broken BIOS for mute LED
hp-inv-led
    HP with broken BIOS for inverted mute LED
hp-mic-led
    HP with mic-mute LED
headset-jack
    Dell Latitude with a 4-pin headset jack
hp-envy-bass
    Pin fixup for HP Envy bass speaker (NID 0x0f)
hp-envy-ts-bass
    Pin fixup for HP Envy TS bass speaker (NID 0x10)
hp-bnb13-eq
    Hardware equalizer setup for HP laptops
hp-envy-ts-bass
    HP Envy TS bass support
auto
    BIOS setup (default)

STAC92HD95
==========
hp-led
    LED support for HP laptops
hp-bass
    Bass HPF setup for HP Spectre 13

STAC9872
========
vaio
    VAIO laptop without SPDIF
auto
    BIOS setup (default)

Cirrus Logic CS4206/4207
========================
mbp53
    MacBook Pro 5,3
mbp55
    MacBook Pro 5,5
imac27
    IMac 27 Inch
imac27_122
    iMac 12,2
apple
    Generic Apple quirk
mbp101
    MacBookPro 10,1
mbp81
    MacBookPro 8,1
mba42
    MacBookAir 4,2
auto
    BIOS setup (default)

Cirrus Logic CS4208
===================
mba6
    MacBook Air 6,1 and 6,2
gpio0
    Enable GPIO 0 amp
mbp11
    MacBookPro 11,2
macmini
    MacMini 7,1
auto
    BIOS setup (default)

VIA VT17xx/VT18xx/VT20xx
========================
auto
    BIOS setup (default)
