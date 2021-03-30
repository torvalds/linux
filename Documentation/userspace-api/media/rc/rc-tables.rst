.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-no-invariants-or-later

.. _Remote_controllers_tables:

************************
Remote controller tables
************************

Unfortunately, for several years, there was no effort to create uniform
IR keycodes for different devices. This caused the same IR keyname to be
mapped completely differently on different IR devices. This resulted
that the same IR keyname to be mapped completely different on different
IR's. Due to that, V4L2 API now specifies a standard for mapping Media
keys on IR.

This standard should be used by both V4L/DVB drivers and userspace
applications

The modules register the remote as keyboard within the linux input
layer. This means that the IR key strokes will look like normal keyboard
key strokes (if CONFIG_INPUT_KEYBOARD is enabled). Using the event
devices (CONFIG_INPUT_EVDEV) it is possible for applications to access
the remote via /dev/input/event devices.


.. _rc_standard_keymap:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: IR default keymapping
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  Key code

       -  Meaning

       -  Key examples on IR

    -  .. row 2

       -  **Numeric keys**

    -  .. row 3

       -  ``KEY_NUMERIC_0``

       -  Keyboard digit 0

       -  0

    -  .. row 4

       -  ``KEY_NUMERIC_1``

       -  Keyboard digit 1

       -  1

    -  .. row 5

       -  ``KEY_NUMERIC_2``

       -  Keyboard digit 2

       -  2

    -  .. row 6

       -  ``KEY_NUMERIC_3``

       -  Keyboard digit 3

       -  3

    -  .. row 7

       -  ``KEY_NUMERIC_4``

       -  Keyboard digit 4

       -  4

    -  .. row 8

       -  ``KEY_NUMERIC_5``

       -  Keyboard digit 5

       -  5

    -  .. row 9

       -  ``KEY_NUMERIC_6``

       -  Keyboard digit 6

       -  6

    -  .. row 10

       -  ``KEY_NUMERIC_7``

       -  Keyboard digit 7

       -  7

    -  .. row 11

       -  ``KEY_NUMERIC_8``

       -  Keyboard digit 8

       -  8

    -  .. row 12

       -  ``KEY_NUMERIC_9``

       -  Keyboard digit 9

       -  9

    -  .. row 13

       -  **Movie play control**

    -  .. row 14

       -  ``KEY_FORWARD``

       -  Instantly advance in time

       -  >> / FORWARD

    -  .. row 15

       -  ``KEY_BACK``

       -  Instantly go back in time

       -  <<< / BACK

    -  .. row 16

       -  ``KEY_FASTFORWARD``

       -  Play movie faster

       -  >>> / FORWARD

    -  .. row 17

       -  ``KEY_REWIND``

       -  Play movie back

       -  REWIND / BACKWARD

    -  .. row 18

       -  ``KEY_NEXT``

       -  Select next chapter / sub-chapter / interval

       -  NEXT / SKIP

    -  .. row 19

       -  ``KEY_PREVIOUS``

       -  Select previous chapter / sub-chapter / interval

       -  << / PREV / PREVIOUS

    -  .. row 20

       -  ``KEY_AGAIN``

       -  Repeat the video or a video interval

       -  REPEAT / LOOP / RECALL

    -  .. row 21

       -  ``KEY_PAUSE``

       -  Pause stream

       -  PAUSE / FREEZE

    -  .. row 22

       -  ``KEY_PLAY``

       -  Play movie at the normal timeshift

       -  NORMAL TIMESHIFT / LIVE / >

    -  .. row 23

       -  ``KEY_PLAYPAUSE``

       -  Alternate between play and pause

       -  PLAY / PAUSE

    -  .. row 24

       -  ``KEY_STOP``

       -  Stop stream

       -  STOP

    -  .. row 25

       -  ``KEY_RECORD``

       -  Start/stop recording stream

       -  CAPTURE / REC / RECORD/PAUSE

    -  .. row 26

       -  ``KEY_CAMERA``

       -  Take a picture of the image

       -  CAMERA ICON / CAPTURE / SNAPSHOT

    -  .. row 27

       -  ``KEY_SHUFFLE``

       -  Enable shuffle mode

       -  SHUFFLE

    -  .. row 28

       -  ``KEY_TIME``

       -  Activate time shift mode

       -  TIME SHIFT

    -  .. row 29

       -  ``KEY_TITLE``

       -  Allow changing the chapter

       -  CHAPTER

    -  .. row 30

       -  ``KEY_SUBTITLE``

       -  Allow changing the subtitle

       -  SUBTITLE

    -  .. row 31

       -  **Image control**

    -  .. row 32

       -  ``KEY_BRIGHTNESSDOWN``

       -  Decrease Brightness

       -  BRIGHTNESS DECREASE

    -  .. row 33

       -  ``KEY_BRIGHTNESSUP``

       -  Increase Brightness

       -  BRIGHTNESS INCREASE

    -  .. row 34

       -  ``KEY_ANGLE``

       -  Switch video camera angle (on videos with more than one angle
	  stored)

       -  ANGLE / SWAP

    -  .. row 35

       -  ``KEY_EPG``

       -  Open the Elecrowonic Play Guide (EPG)

       -  EPG / GUIDE

    -  .. row 36

       -  ``KEY_TEXT``

       -  Activate/change closed caption mode

       -  CLOSED CAPTION/TELETEXT / DVD TEXT / TELETEXT / TTX

    -  .. row 37

       -  **Audio control**

    -  .. row 38

       -  ``KEY_AUDIO``

       -  Change audio source

       -  AUDIO SOURCE / AUDIO / MUSIC

    -  .. row 39

       -  ``KEY_MUTE``

       -  Mute/unmute audio

       -  MUTE / DEMUTE / UNMUTE

    -  .. row 40

       -  ``KEY_VOLUMEDOWN``

       -  Decrease volume

       -  VOLUME- / VOLUME DOWN

    -  .. row 41

       -  ``KEY_VOLUMEUP``

       -  Increase volume

       -  VOLUME+ / VOLUME UP

    -  .. row 42

       -  ``KEY_MODE``

       -  Change sound mode

       -  MONO/STEREO

    -  .. row 43

       -  ``KEY_LANGUAGE``

       -  Select Language

       -  1ST / 2ND LANGUAGE / DVD LANG / MTS/SAP / MTS SEL

    -  .. row 44

       -  **Channel control**

    -  .. row 45

       -  ``KEY_CHANNEL``

       -  Go to the next favorite channel

       -  ALT / CHANNEL / CH SURFING / SURF / FAV

    -  .. row 46

       -  ``KEY_CHANNELDOWN``

       -  Decrease channel sequentially

       -  CHANNEL - / CHANNEL DOWN / DOWN

    -  .. row 47

       -  ``KEY_CHANNELUP``

       -  Increase channel sequentially

       -  CHANNEL + / CHANNEL UP / UP

    -  .. row 48

       -  ``KEY_DIGITS``

       -  Use more than one digit for channel

       -  PLUS / 100/ 1xx / xxx / -/-- / Single Double Triple Digit

    -  .. row 49

       -  ``KEY_SEARCH``

       -  Start channel autoscan

       -  SCAN / AUTOSCAN

    -  .. row 50

       -  **Colored keys**

    -  .. row 51

       -  ``KEY_BLUE``

       -  IR Blue key

       -  BLUE

    -  .. row 52

       -  ``KEY_GREEN``

       -  IR Green Key

       -  GREEN

    -  .. row 53

       -  ``KEY_RED``

       -  IR Red key

       -  RED

    -  .. row 54

       -  ``KEY_YELLOW``

       -  IR Yellow key

       -  YELLOW

    -  .. row 55

       -  **Media selection**

    -  .. row 56

       -  ``KEY_CD``

       -  Change input source to Compact Disc

       -  CD

    -  .. row 57

       -  ``KEY_DVD``

       -  Change input to DVD

       -  DVD / DVD MENU

    -  .. row 58

       -  ``KEY_EJECTCLOSECD``

       -  Open/close the CD/DVD player

       -  -> ) / CLOSE / OPEN

    -  .. row 59

       -  ``KEY_MEDIA``

       -  Turn on/off Media application

       -  PC/TV / TURN ON/OFF APP

    -  .. row 60

       -  ``KEY_PC``

       -  Selects from TV to PC

       -  PC

    -  .. row 61

       -  ``KEY_RADIO``

       -  Put into AM/FM radio mode

       -  RADIO / TV/FM / TV/RADIO / FM / FM/RADIO

    -  .. row 62

       -  ``KEY_TV``

       -  Select tv mode

       -  TV / LIVE TV

    -  .. row 63

       -  ``KEY_TV2``

       -  Select Cable mode

       -  AIR/CBL

    -  .. row 64

       -  ``KEY_VCR``

       -  Select VCR mode

       -  VCR MODE / DTR

    -  .. row 65

       -  ``KEY_VIDEO``

       -  Alternate between input modes

       -  SOURCE / SELECT / DISPLAY / SWITCH INPUTS / VIDEO

    -  .. row 66

       -  **Power control**

    -  .. row 67

       -  ``KEY_POWER``

       -  Turn on/off computer

       -  SYSTEM POWER / COMPUTER POWER

    -  .. row 68

       -  ``KEY_POWER2``

       -  Turn on/off application

       -  TV ON/OFF / POWER

    -  .. row 69

       -  ``KEY_SLEEP``

       -  Activate sleep timer

       -  SLEEP / SLEEP TIMER

    -  .. row 70

       -  ``KEY_SUSPEND``

       -  Put computer into suspend mode

       -  STANDBY / SUSPEND

    -  .. row 71

       -  **Window control**

    -  .. row 72

       -  ``KEY_CLEAR``

       -  Stop stream and return to default input video/audio

       -  CLEAR / RESET / BOSS KEY

    -  .. row 73

       -  ``KEY_CYCLEWINDOWS``

       -  Minimize windows and move to the next one

       -  ALT-TAB / MINIMIZE / DESKTOP

    -  .. row 74

       -  ``KEY_FAVORITES``

       -  Open the favorites stream window

       -  TV WALL / Favorites

    -  .. row 75

       -  ``KEY_MENU``

       -  Call application menu

       -  2ND CONTROLS (USA: MENU) / DVD/MENU / SHOW/HIDE CTRL

    -  .. row 76

       -  ``KEY_NEW``

       -  Open/Close Picture in Picture

       -  PIP

    -  .. row 77

       -  ``KEY_OK``

       -  Send a confirmation code to application

       -  OK / ENTER / RETURN

    -  .. row 78

       -  ``KEY_ASPECT_RATIO``

       -  Select screen aspect ratio

       -  4:3 16:9 SELECT

    -  .. row 79

       -  ``KEY_FULL_SCREEN``

       -  Put device into zoom/full screen mode

       -  ZOOM / FULL SCREEN / ZOOM+ / HIDE PANNEL / SWITCH

    -  .. row 80

       -  **Navigation keys**

    -  .. row 81

       -  ``KEY_ESC``

       -  Cancel current operation

       -  CANCEL / BACK

    -  .. row 82

       -  ``KEY_HELP``

       -  Open a Help window

       -  HELP

    -  .. row 83

       -  ``KEY_HOMEPAGE``

       -  Navigate to Homepage

       -  HOME

    -  .. row 84

       -  ``KEY_INFO``

       -  Open On Screen Display

       -  DISPLAY INFORMATION / OSD

    -  .. row 85

       -  ``KEY_WWW``

       -  Open the default browser

       -  WEB

    -  .. row 86

       -  ``KEY_UP``

       -  Up key

       -  UP

    -  .. row 87

       -  ``KEY_DOWN``

       -  Down key

       -  DOWN

    -  .. row 88

       -  ``KEY_LEFT``

       -  Left key

       -  LEFT

    -  .. row 89

       -  ``KEY_RIGHT``

       -  Right key

       -  RIGHT

    -  .. row 90

       -  **Miscellaneous keys**

    -  .. row 91

       -  ``KEY_DOT``

       -  Return a dot

       -  .

    -  .. row 92

       -  ``KEY_FN``

       -  Select a function

       -  FUNCTION


It should be noted that, sometimes, there some fundamental missing keys
at some cheaper IR's. Due to that, it is recommended to:


.. _rc_keymap_notes:

.. flat-table:: Notes
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  On simpler IR's, without separate channel keys, you need to map UP
	  as ``KEY_CHANNELUP``

    -  .. row 2

       -  On simpler IR's, without separate channel keys, you need to map
	  DOWN as ``KEY_CHANNELDOWN``

    -  .. row 3

       -  On simpler IR's, without separate volume keys, you need to map
	  LEFT as ``KEY_VOLUMEDOWN``

    -  .. row 4

       -  On simpler IR's, without separate volume keys, you need to map
	  RIGHT as ``KEY_VOLUMEUP``
