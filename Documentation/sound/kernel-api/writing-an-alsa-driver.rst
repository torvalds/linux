======================
Writing an ALSA Driver
======================

:Author: Takashi Iwai <tiwai@suse.de>

Preface
=======

This document describes how to write an `ALSA (Advanced Linux Sound
Architecture) <http://www.alsa-project.org/>`__ driver. The document
focuses mainly on PCI soundcards. In the case of other device types, the
API might be different, too. However, at least the ALSA kernel API is
consistent, and therefore it would be still a bit help for writing them.

This document targets people who already have enough C language skills
and have basic linux kernel programming knowledge. This document doesn't
explain the general topic of linux kernel coding and doesn't cover
low-level driver implementation details. It only describes the standard
way to write a PCI sound driver on ALSA.

File Tree Structure
===================

General
-------

The file tree structure of ALSA driver is depicted below::

            sound
                    /core
                            /oss
                            /seq
                                    /oss
                    /include
                    /drivers
                            /mpu401
                            /opl3
                    /i2c
                    /synth
                            /emux
                    /pci
                            /(cards)
                    /isa
                            /(cards)
                    /arm
                    /ppc
                    /sparc
                    /usb
                    /pcmcia /(cards)
                    /soc
                    /oss


core directory
--------------

This directory contains the middle layer which is the heart of ALSA
drivers. In this directory, the native ALSA modules are stored. The
sub-directories contain different modules and are dependent upon the
kernel config.

core/oss
~~~~~~~~

The code for OSS PCM and mixer emulation modules is stored in this
directory. The OSS rawmidi emulation is included in the ALSA rawmidi
code since it's quite small. The sequencer code is stored in
``core/seq/oss`` directory (see `below <core/seq/oss_>`__).

core/seq
~~~~~~~~

This directory and its sub-directories are for the ALSA sequencer. This
directory contains the sequencer core and primary sequencer modules such
as snd-seq-midi, snd-seq-virmidi, etc. They are compiled only when
``CONFIG_SND_SEQUENCER`` is set in the kernel config.

core/seq/oss
~~~~~~~~~~~~

This contains the OSS sequencer emulation code.

include directory
-----------------

This is the place for the public header files of ALSA drivers, which are
to be exported to user-space, or included by several files in different
directories. Basically, the private header files should not be placed in
this directory, but you may still find files there, due to historical
reasons :)

drivers directory
-----------------

This directory contains code shared among different drivers on different
architectures. They are hence supposed not to be architecture-specific.
For example, the dummy PCM driver and the serial MIDI driver are found
in this directory. In the sub-directories, there is code for components
which are independent from bus and cpu architectures.

drivers/mpu401
~~~~~~~~~~~~~~

The MPU401 and MPU401-UART modules are stored here.

drivers/opl3 and opl4
~~~~~~~~~~~~~~~~~~~~~

The OPL3 and OPL4 FM-synth stuff is found here.

i2c directory
-------------

This contains the ALSA i2c components.

Although there is a standard i2c layer on Linux, ALSA has its own i2c
code for some cards, because the soundcard needs only a simple operation
and the standard i2c API is too complicated for such a purpose.

synth directory
---------------

This contains the synth middle-level modules.

So far, there is only Emu8000/Emu10k1 synth driver under the
``synth/emux`` sub-directory.

pci directory
-------------

This directory and its sub-directories hold the top-level card modules
for PCI soundcards and the code specific to the PCI BUS.

The drivers compiled from a single file are stored directly in the pci
directory, while the drivers with several source files are stored on
their own sub-directory (e.g. emu10k1, ice1712).

isa directory
-------------

This directory and its sub-directories hold the top-level card modules
for ISA soundcards.

arm, ppc, and sparc directories
-------------------------------

They are used for top-level card modules which are specific to one of
these architectures.

usb directory
-------------

This directory contains the USB-audio driver.
The USB MIDI driver is integrated in the usb-audio driver.

pcmcia directory
----------------

The PCMCIA, especially PCCard drivers will go here. CardBus drivers will
be in the pci directory, because their API is identical to that of
standard PCI cards.

soc directory
-------------

This directory contains the codes for ASoC (ALSA System on Chip)
layer including ASoC core, codec and machine drivers.

oss directory
-------------

This contains OSS/Lite code.
At the time of writing, all code has been removed except for dmasound
on m68k.


Basic Flow for PCI Drivers
==========================

Outline
-------

The minimum flow for PCI soundcards is as follows:

-  define the PCI ID table (see the section `PCI Entries`_).

-  create ``probe`` callback.

-  create ``remove`` callback.

-  create a struct pci_driver structure
   containing the three pointers above.

-  create an ``init`` function just calling the
   :c:func:`pci_register_driver()` to register the pci_driver
   table defined above.

-  create an ``exit`` function to call the
   :c:func:`pci_unregister_driver()` function.

Full Code Example
-----------------

The code example is shown below. Some parts are kept unimplemented at
this moment but will be filled in the next sections. The numbers in the
comment lines of the :c:func:`snd_mychip_probe()` function refer
to details explained in the following section.

::

      #include <linux/init.h>
      #include <linux/pci.h>
      #include <linux/slab.h>
      #include <sound/core.h>
      #include <sound/initval.h>

      /* module parameters (see "Module Parameters") */
      /* SNDRV_CARDS: maximum number of cards supported by this module */
      static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
      static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
      static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

      /* definition of the chip-specific record */
      struct mychip {
              struct snd_card *card;
              /* the rest of the implementation will be in section
               * "PCI Resource Management"
               */
      };

      /* chip-specific destructor
       * (see "PCI Resource Management")
       */
      static int snd_mychip_free(struct mychip *chip)
      {
              .... /* will be implemented later... */
      }

      /* component-destructor
       * (see "Management of Cards and Components")
       */
      static int snd_mychip_dev_free(struct snd_device *device)
      {
              return snd_mychip_free(device->device_data);
      }

      /* chip-specific constructor
       * (see "Management of Cards and Components")
       */
      static int snd_mychip_create(struct snd_card *card,
                                   struct pci_dev *pci,
                                   struct mychip **rchip)
      {
              struct mychip *chip;
              int err;
              static const struct snd_device_ops ops = {
                     .dev_free = snd_mychip_dev_free,
              };

              *rchip = NULL;

              /* check PCI availability here
               * (see "PCI Resource Management")
               */
              ....

              /* allocate a chip-specific data with zero filled */
              chip = kzalloc(sizeof(*chip), GFP_KERNEL);
              if (chip == NULL)
                      return -ENOMEM;

              chip->card = card;

              /* rest of initialization here; will be implemented
               * later, see "PCI Resource Management"
               */
              ....

              err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
              if (err < 0) {
                      snd_mychip_free(chip);
                      return err;
              }

              *rchip = chip;
              return 0;
      }

      /* constructor -- see "Driver Constructor" sub-section */
      static int snd_mychip_probe(struct pci_dev *pci,
                                  const struct pci_device_id *pci_id)
      {
              static int dev;
              struct snd_card *card;
              struct mychip *chip;
              int err;

              /* (1) */
              if (dev >= SNDRV_CARDS)
                      return -ENODEV;
              if (!enable[dev]) {
                      dev++;
                      return -ENOENT;
              }

              /* (2) */
              err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
                                 0, &card);
              if (err < 0)
                      return err;

              /* (3) */
              err = snd_mychip_create(card, pci, &chip);
              if (err < 0)
                      goto error;

              /* (4) */
              strcpy(card->driver, "My Chip");
              strcpy(card->shortname, "My Own Chip 123");
              sprintf(card->longname, "%s at 0x%lx irq %i",
                      card->shortname, chip->port, chip->irq);

              /* (5) */
              .... /* implemented later */

              /* (6) */
              err = snd_card_register(card);
              if (err < 0)
                      goto error;

              /* (7) */
              pci_set_drvdata(pci, card);
              dev++;
              return 0;

      error:
              snd_card_free(card);
              return err;
      }

      /* destructor -- see the "Destructor" sub-section */
      static void snd_mychip_remove(struct pci_dev *pci)
      {
              snd_card_free(pci_get_drvdata(pci));
      }



Driver Constructor
------------------

The real constructor of PCI drivers is the ``probe`` callback. The
``probe`` callback and other component-constructors which are called
from the ``probe`` callback cannot be used with the ``__init`` prefix
because any PCI device could be a hotplug device.

In the ``probe`` callback, the following scheme is often used.

1) Check and increment the device index.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

  static int dev;
  ....
  if (dev >= SNDRV_CARDS)
          return -ENODEV;
  if (!enable[dev]) {
          dev++;
          return -ENOENT;
  }


where ``enable[dev]`` is the module option.

Each time the ``probe`` callback is called, check the availability of
the device. If not available, simply increment the device index and
return. dev will be incremented also later (`step 7
<7) Set the PCI driver data and return zero._>`__).

2) Create a card instance
~~~~~~~~~~~~~~~~~~~~~~~~~

::

  struct snd_card *card;
  int err;
  ....
  err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
                     0, &card);


The details will be explained in the section `Management of Cards and
Components`_.

3) Create a main component
~~~~~~~~~~~~~~~~~~~~~~~~~~

In this part, the PCI resources are allocated::

  struct mychip *chip;
  ....
  err = snd_mychip_create(card, pci, &chip);
  if (err < 0)
          goto error;

The details will be explained in the section `PCI Resource
Management`_.

When something goes wrong, the probe function needs to deal with the
error.  In this example, we have a single error handling path placed
at the end of the function::

  error:
          snd_card_free(card);
          return err;

Since each component can be properly freed, the single
:c:func:`snd_card_free()` call should suffice in most cases.


4) Set the driver ID and name strings.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

  strcpy(card->driver, "My Chip");
  strcpy(card->shortname, "My Own Chip 123");
  sprintf(card->longname, "%s at 0x%lx irq %i",
          card->shortname, chip->port, chip->irq);

The driver field holds the minimal ID string of the chip. This is used
by alsa-lib's configurator, so keep it simple but unique. Even the
same driver can have different driver IDs to distinguish the
functionality of each chip type.

The shortname field is a string shown as more verbose name. The longname
field contains the information shown in ``/proc/asound/cards``.

5) Create other components, such as mixer, MIDI, etc.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here you define the basic components such as `PCM <PCM Interface_>`__,
mixer (e.g. `AC97 <API for AC97 Codec_>`__), MIDI (e.g.
`MPU-401 <MIDI (MPU401-UART) Interface_>`__), and other interfaces.
Also, if you want a `proc file <Proc Interface_>`__, define it here,
too.

6) Register the card instance.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

  err = snd_card_register(card);
  if (err < 0)
          goto error;

Will be explained in the section `Management of Cards and
Components`_, too.

7) Set the PCI driver data and return zero.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

  pci_set_drvdata(pci, card);
  dev++;
  return 0;

In the above, the card record is stored. This pointer is used in the
remove callback and power-management callbacks, too.

Destructor
----------

The destructor, the remove callback, simply releases the card instance.
Then the ALSA middle layer will release all the attached components
automatically.

It would be typically just calling :c:func:`snd_card_free()`::

  static void snd_mychip_remove(struct pci_dev *pci)
  {
          snd_card_free(pci_get_drvdata(pci));
  }


The above code assumes that the card pointer is set to the PCI driver
data.

Header Files
------------

For the above example, at least the following include files are
necessary::

  #include <linux/init.h>
  #include <linux/pci.h>
  #include <linux/slab.h>
  #include <sound/core.h>
  #include <sound/initval.h>

where the last one is necessary only when module options are defined
in the source file. If the code is split into several files, the files
without module options don't need them.

In addition to these headers, you'll need ``<linux/interrupt.h>`` for
interrupt handling, and ``<linux/io.h>`` for I/O access. If you use the
:c:func:`mdelay()` or :c:func:`udelay()` functions, you'll need
to include ``<linux/delay.h>`` too.

The ALSA interfaces like the PCM and control APIs are defined in other
``<sound/xxx.h>`` header files. They have to be included after
``<sound/core.h>``.

Management of Cards and Components
==================================

Card Instance
-------------

For each soundcard, a “card” record must be allocated.

A card record is the headquarters of the soundcard. It manages the whole
list of devices (components) on the soundcard, such as PCM, mixers,
MIDI, synthesizer, and so on. Also, the card record holds the ID and the
name strings of the card, manages the root of proc files, and controls
the power-management states and hotplug disconnections. The component
list on the card record is used to manage the correct release of
resources at destruction.

As mentioned above, to create a card instance, call
:c:func:`snd_card_new()`::

  struct snd_card *card;
  int err;
  err = snd_card_new(&pci->dev, index, id, module, extra_size, &card);


The function takes six arguments: the parent device pointer, the
card-index number, the id string, the module pointer (usually
``THIS_MODULE``), the size of extra-data space, and the pointer to
return the card instance. The extra_size argument is used to allocate
card->private_data for the chip-specific data. Note that these data are
allocated by :c:func:`snd_card_new()`.

The first argument, the pointer of struct device, specifies the parent
device. For PCI devices, typically ``&pci->`` is passed there.

Components
----------

After the card is created, you can attach the components (devices) to
the card instance. In an ALSA driver, a component is represented as a
struct snd_device object. A component
can be a PCM instance, a control interface, a raw MIDI interface, etc.
Each such instance has one component entry.

A component can be created via the :c:func:`snd_device_new()`
function::

  snd_device_new(card, SNDRV_DEV_XXX, chip, &ops);

This takes the card pointer, the device-level (``SNDRV_DEV_XXX``), the
data pointer, and the callback pointers (``&ops``). The device-level
defines the type of components and the order of registration and
de-registration. For most components, the device-level is already
defined. For a user-defined component, you can use
``SNDRV_DEV_LOWLEVEL``.

This function itself doesn't allocate the data space. The data must be
allocated manually beforehand, and its pointer is passed as the
argument. This pointer (``chip`` in the above example) is used as the
identifier for the instance.

Each pre-defined ALSA component such as AC97 and PCM calls
:c:func:`snd_device_new()` inside its constructor. The destructor
for each component is defined in the callback pointers. Hence, you don't
need to take care of calling a destructor for such a component.

If you wish to create your own component, you need to set the destructor
function to the dev_free callback in the ``ops``, so that it can be
released automatically via :c:func:`snd_card_free()`. The next
example will show an implementation of chip-specific data.

Chip-Specific Data
------------------

Chip-specific information, e.g. the I/O port address, its resource
pointer, or the irq number, is stored in the chip-specific record::

  struct mychip {
          ....
  };


In general, there are two ways of allocating the chip record.

1. Allocating via :c:func:`snd_card_new()`.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As mentioned above, you can pass the extra-data-length to the 5th
argument of :c:func:`snd_card_new()`, e.g.::

  err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
                     sizeof(struct mychip), &card);

struct mychip is the type of the chip record.

In return, the allocated record can be accessed as

::

  struct mychip *chip = card->private_data;

With this method, you don't have to allocate twice. The record is
released together with the card instance.

2. Allocating an extra device.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

After allocating a card instance via :c:func:`snd_card_new()`
(with ``0`` on the 4th arg), call :c:func:`kzalloc()`::

  struct snd_card *card;
  struct mychip *chip;
  err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
                     0, &card);
  .....
  chip = kzalloc(sizeof(*chip), GFP_KERNEL);

The chip record should have the field to hold the card pointer at least,

::

  struct mychip {
          struct snd_card *card;
          ....
  };


Then, set the card pointer in the returned chip instance::

  chip->card = card;

Next, initialize the fields, and register this chip record as a
low-level device with a specified ``ops``::

  static const struct snd_device_ops ops = {
          .dev_free =        snd_mychip_dev_free,
  };
  ....
  snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);

:c:func:`snd_mychip_dev_free()` is the device-destructor
function, which will call the real destructor::

  static int snd_mychip_dev_free(struct snd_device *device)
  {
          return snd_mychip_free(device->device_data);
  }

where :c:func:`snd_mychip_free()` is the real destructor.

The demerit of this method is the obviously larger amount of code.
The merit is, however, that you can trigger your own callback at
registering and disconnecting the card via a setting in snd_device_ops.
About registering and disconnecting the card, see the subsections
below.


Registration and Release
------------------------

After all components are assigned, register the card instance by calling
:c:func:`snd_card_register()`. Access to the device files is
enabled at this point. That is, before
:c:func:`snd_card_register()` is called, the components are safely
inaccessible from external side. If this call fails, exit the probe
function after releasing the card via :c:func:`snd_card_free()`.

For releasing the card instance, you can call simply
:c:func:`snd_card_free()`. As mentioned earlier, all components
are released automatically by this call.

For a device which allows hotplugging, you can use
:c:func:`snd_card_free_when_closed()`. This one will postpone
the destruction until all devices are closed.

PCI Resource Management
=======================

Full Code Example
-----------------

In this section, we'll complete the chip-specific constructor,
destructor and PCI entries. Example code is shown first, below::

      struct mychip {
              struct snd_card *card;
              struct pci_dev *pci;

              unsigned long port;
              int irq;
      };

      static int snd_mychip_free(struct mychip *chip)
      {
              /* disable hardware here if any */
              .... /* (not implemented in this document) */

              /* release the irq */
              if (chip->irq >= 0)
                      free_irq(chip->irq, chip);
              /* release the I/O ports & memory */
              pci_release_regions(chip->pci);
              /* disable the PCI entry */
              pci_disable_device(chip->pci);
              /* release the data */
              kfree(chip);
              return 0;
      }

      /* chip-specific constructor */
      static int snd_mychip_create(struct snd_card *card,
                                   struct pci_dev *pci,
                                   struct mychip **rchip)
      {
              struct mychip *chip;
              int err;
              static const struct snd_device_ops ops = {
                     .dev_free = snd_mychip_dev_free,
              };

              *rchip = NULL;

              /* initialize the PCI entry */
              err = pci_enable_device(pci);
              if (err < 0)
                      return err;
              /* check PCI availability (28bit DMA) */
              if (pci_set_dma_mask(pci, DMA_BIT_MASK(28)) < 0 ||
                  pci_set_consistent_dma_mask(pci, DMA_BIT_MASK(28)) < 0) {
                      printk(KERN_ERR "error to set 28bit mask DMA\n");
                      pci_disable_device(pci);
                      return -ENXIO;
              }

              chip = kzalloc(sizeof(*chip), GFP_KERNEL);
              if (chip == NULL) {
                      pci_disable_device(pci);
                      return -ENOMEM;
              }

              /* initialize the stuff */
              chip->card = card;
              chip->pci = pci;
              chip->irq = -1;

              /* (1) PCI resource allocation */
              err = pci_request_regions(pci, "My Chip");
              if (err < 0) {
                      kfree(chip);
                      pci_disable_device(pci);
                      return err;
              }
              chip->port = pci_resource_start(pci, 0);
              if (request_irq(pci->irq, snd_mychip_interrupt,
                              IRQF_SHARED, KBUILD_MODNAME, chip)) {
                      printk(KERN_ERR "cannot grab irq %d\n", pci->irq);
                      snd_mychip_free(chip);
                      return -EBUSY;
              }
              chip->irq = pci->irq;
              card->sync_irq = chip->irq;

              /* (2) initialization of the chip hardware */
              .... /*   (not implemented in this document) */

              err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
              if (err < 0) {
                      snd_mychip_free(chip);
                      return err;
              }

              *rchip = chip;
              return 0;
      }

      /* PCI IDs */
      static struct pci_device_id snd_mychip_ids[] = {
              { PCI_VENDOR_ID_FOO, PCI_DEVICE_ID_BAR,
                PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },
              ....
              { 0, }
      };
      MODULE_DEVICE_TABLE(pci, snd_mychip_ids);

      /* pci_driver definition */
      static struct pci_driver driver = {
              .name = KBUILD_MODNAME,
              .id_table = snd_mychip_ids,
              .probe = snd_mychip_probe,
              .remove = snd_mychip_remove,
      };

      /* module initialization */
      static int __init alsa_card_mychip_init(void)
      {
              return pci_register_driver(&driver);
      }

      /* module clean up */
      static void __exit alsa_card_mychip_exit(void)
      {
              pci_unregister_driver(&driver);
      }

      module_init(alsa_card_mychip_init)
      module_exit(alsa_card_mychip_exit)

      EXPORT_NO_SYMBOLS; /* for old kernels only */

Some Hafta's
------------

The allocation of PCI resources is done in the ``probe`` function, and
usually an extra :c:func:`xxx_create()` function is written for this
purpose.

In the case of PCI devices, you first have to call the
:c:func:`pci_enable_device()` function before allocating
resources. Also, you need to set the proper PCI DMA mask to limit the
accessed I/O range. In some cases, you might need to call
:c:func:`pci_set_master()` function, too.

Suppose a 28bit mask, the code to be added would look like::

  err = pci_enable_device(pci);
  if (err < 0)
          return err;
  if (pci_set_dma_mask(pci, DMA_BIT_MASK(28)) < 0 ||
      pci_set_consistent_dma_mask(pci, DMA_BIT_MASK(28)) < 0) {
          printk(KERN_ERR "error to set 28bit mask DMA\n");
          pci_disable_device(pci);
          return -ENXIO;
  }
  

Resource Allocation
-------------------

The allocation of I/O ports and irqs is done via standard kernel
functions.  These resources must be released in the destructor
function (see below).

Now assume that the PCI device has an I/O port with 8 bytes and an
interrupt. Then struct mychip will have the
following fields::

  struct mychip {
          struct snd_card *card;

          unsigned long port;
          int irq;
  };


For an I/O port (and also a memory region), you need to have the
resource pointer for the standard resource management. For an irq, you
have to keep only the irq number (integer). But you need to initialize
this number to -1 before actual allocation, since irq 0 is valid. The
port address and its resource pointer can be initialized as null by
:c:func:`kzalloc()` automatically, so you don't have to take care of
resetting them.

The allocation of an I/O port is done like this::

  err = pci_request_regions(pci, "My Chip");
  if (err < 0) { 
          kfree(chip);
          pci_disable_device(pci);
          return err;
  }
  chip->port = pci_resource_start(pci, 0);

It will reserve the I/O port region of 8 bytes of the given PCI device.
The returned value, ``chip->res_port``, is allocated via
:c:func:`kmalloc()` by :c:func:`request_region()`. The pointer
must be released via :c:func:`kfree()`, but there is a problem with
this. This issue will be explained later.

The allocation of an interrupt source is done like this::

  if (request_irq(pci->irq, snd_mychip_interrupt,
                  IRQF_SHARED, KBUILD_MODNAME, chip)) {
          printk(KERN_ERR "cannot grab irq %d\n", pci->irq);
          snd_mychip_free(chip);
          return -EBUSY;
  }
  chip->irq = pci->irq;

where :c:func:`snd_mychip_interrupt()` is the interrupt handler
defined `later <PCM Interrupt Handler_>`__. Note that
``chip->irq`` should be defined only when :c:func:`request_irq()`
succeeded.

On the PCI bus, interrupts can be shared. Thus, ``IRQF_SHARED`` is used
as the interrupt flag of :c:func:`request_irq()`.

The last argument of :c:func:`request_irq()` is the data pointer
passed to the interrupt handler. Usually, the chip-specific record is
used for that, but you can use what you like, too.

I won't give details about the interrupt handler at this point, but at
least its appearance can be explained now. The interrupt handler looks
usually as follows::

  static irqreturn_t snd_mychip_interrupt(int irq, void *dev_id)
  {
          struct mychip *chip = dev_id;
          ....
          return IRQ_HANDLED;
  }

After requesting the IRQ, you can passed it to ``card->sync_irq``
field::

          card->irq = chip->irq;

This allows the PCM core to automatically call
:c:func:`synchronize_irq()` at the right time, like before ``hw_free``.
See the later section `sync_stop callback`_ for details.

Now let's write the corresponding destructor for the resources above.
The role of destructor is simple: disable the hardware (if already
activated) and release the resources. So far, we have no hardware part,
so the disabling code is not written here.

To release the resources, the “check-and-release” method is a safer way.
For the interrupt, do like this::

  if (chip->irq >= 0)
          free_irq(chip->irq, chip);

Since the irq number can start from 0, you should initialize
``chip->irq`` with a negative value (e.g. -1), so that you can check
the validity of the irq number as above.

When you requested I/O ports or memory regions via
:c:func:`pci_request_region()` or
:c:func:`pci_request_regions()` like in this example, release the
resource(s) using the corresponding function,
:c:func:`pci_release_region()` or
:c:func:`pci_release_regions()`::

  pci_release_regions(chip->pci);

When you requested manually via :c:func:`request_region()` or
:c:func:`request_mem_region()`, you can release it via
:c:func:`release_resource()`. Suppose that you keep the resource
pointer returned from :c:func:`request_region()` in
chip->res_port, the release procedure looks like::

  release_and_free_resource(chip->res_port);

Don't forget to call :c:func:`pci_disable_device()` before the
end.

And finally, release the chip-specific record::

  kfree(chip);

We didn't implement the hardware disabling part above. If you
need to do this, please note that the destructor may be called even
before the initialization of the chip is completed. It would be better
to have a flag to skip hardware disabling if the hardware was not
initialized yet.

When the chip-data is assigned to the card using
:c:func:`snd_device_new()` with ``SNDRV_DEV_LOWLELVEL``, its
destructor is called last. That is, it is assured that all other
components like PCMs and controls have already been released. You don't
have to stop PCMs, etc. explicitly, but just call low-level hardware
stopping.

The management of a memory-mapped region is almost as same as the
management of an I/O port. You'll need two fields as follows::

  struct mychip {
          ....
          unsigned long iobase_phys;
          void __iomem *iobase_virt;
  };

and the allocation would look like below::

  err = pci_request_regions(pci, "My Chip");
  if (err < 0) {
          kfree(chip);
          return err;
  }
  chip->iobase_phys = pci_resource_start(pci, 0);
  chip->iobase_virt = ioremap(chip->iobase_phys,
                                      pci_resource_len(pci, 0));

and the corresponding destructor would be::

  static int snd_mychip_free(struct mychip *chip)
  {
          ....
          if (chip->iobase_virt)
                  iounmap(chip->iobase_virt);
          ....
          pci_release_regions(chip->pci);
          ....
  }

Of course, a modern way with :c:func:`pci_iomap()` will make things a
bit easier, too::

  err = pci_request_regions(pci, "My Chip");
  if (err < 0) {
          kfree(chip);
          return err;
  }
  chip->iobase_virt = pci_iomap(pci, 0, 0);

which is paired with :c:func:`pci_iounmap()` at destructor.


PCI Entries
-----------

So far, so good. Let's finish the missing PCI stuff. At first, we need a
struct pci_device_id table for
this chipset. It's a table of PCI vendor/device ID number, and some
masks.

For example::

  static struct pci_device_id snd_mychip_ids[] = {
          { PCI_VENDOR_ID_FOO, PCI_DEVICE_ID_BAR,
            PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },
          ....
          { 0, }
  };
  MODULE_DEVICE_TABLE(pci, snd_mychip_ids);

The first and second fields of the struct pci_device_id are the vendor
and device IDs. If you have no reason to filter the matching devices, you can
leave the remaining fields as above. The last field of the
struct pci_device_id contains private data for this entry. You can specify
any value here, for example, to define specific operations for supported
device IDs. Such an example is found in the intel8x0 driver.

The last entry of this list is the terminator. You must specify this
all-zero entry.

Then, prepare the struct pci_driver
record::

  static struct pci_driver driver = {
          .name = KBUILD_MODNAME,
          .id_table = snd_mychip_ids,
          .probe = snd_mychip_probe,
          .remove = snd_mychip_remove,
  };

The ``probe`` and ``remove`` functions have already been defined in
the previous sections. The ``name`` field is the name string of this
device. Note that you must not use slashes (“/”) in this string.

And at last, the module entries::

  static int __init alsa_card_mychip_init(void)
  {
          return pci_register_driver(&driver);
  }

  static void __exit alsa_card_mychip_exit(void)
  {
          pci_unregister_driver(&driver);
  }

  module_init(alsa_card_mychip_init)
  module_exit(alsa_card_mychip_exit)

Note that these module entries are tagged with ``__init`` and ``__exit``
prefixes.

That's all!

PCM Interface
=============

General
-------

The PCM middle layer of ALSA is quite powerful and it is only necessary
for each driver to implement the low-level functions to access its
hardware.

To access the PCM layer, you need to include ``<sound/pcm.h>``
first. In addition, ``<sound/pcm_params.h>`` might be needed if you
access some functions related with hw_param.

Each card device can have up to four PCM instances. A PCM instance
corresponds to a PCM device file. The limitation of number of instances
comes only from the available bit size of Linux' device numbers.
Once 64bit device numbers are used, we'll have more PCM instances
available.

A PCM instance consists of PCM playback and capture streams, and each
PCM stream consists of one or more PCM substreams. Some soundcards
support multiple playback functions. For example, emu10k1 has a PCM
playback of 32 stereo substreams. In this case, at each open, a free
substream is (usually) automatically chosen and opened. Meanwhile, when
only one substream exists and it was already opened, a subsequent open
will either block or error with ``EAGAIN`` according to the file open
mode. But you don't have to care about such details in your driver. The
PCM middle layer will take care of such work.

Full Code Example
-----------------

The example code below does not include any hardware access routines but
shows only the skeleton, how to build up the PCM interfaces::

      #include <sound/pcm.h>
      ....

      /* hardware definition */
      static struct snd_pcm_hardware snd_mychip_playback_hw = {
              .info = (SNDRV_PCM_INFO_MMAP |
                       SNDRV_PCM_INFO_INTERLEAVED |
                       SNDRV_PCM_INFO_BLOCK_TRANSFER |
                       SNDRV_PCM_INFO_MMAP_VALID),
              .formats =          SNDRV_PCM_FMTBIT_S16_LE,
              .rates =            SNDRV_PCM_RATE_8000_48000,
              .rate_min =         8000,
              .rate_max =         48000,
              .channels_min =     2,
              .channels_max =     2,
              .buffer_bytes_max = 32768,
              .period_bytes_min = 4096,
              .period_bytes_max = 32768,
              .periods_min =      1,
              .periods_max =      1024,
      };

      /* hardware definition */
      static struct snd_pcm_hardware snd_mychip_capture_hw = {
              .info = (SNDRV_PCM_INFO_MMAP |
                       SNDRV_PCM_INFO_INTERLEAVED |
                       SNDRV_PCM_INFO_BLOCK_TRANSFER |
                       SNDRV_PCM_INFO_MMAP_VALID),
              .formats =          SNDRV_PCM_FMTBIT_S16_LE,
              .rates =            SNDRV_PCM_RATE_8000_48000,
              .rate_min =         8000,
              .rate_max =         48000,
              .channels_min =     2,
              .channels_max =     2,
              .buffer_bytes_max = 32768,
              .period_bytes_min = 4096,
              .period_bytes_max = 32768,
              .periods_min =      1,
              .periods_max =      1024,
      };

      /* open callback */
      static int snd_mychip_playback_open(struct snd_pcm_substream *substream)
      {
              struct mychip *chip = snd_pcm_substream_chip(substream);
              struct snd_pcm_runtime *runtime = substream->runtime;

              runtime->hw = snd_mychip_playback_hw;
              /* more hardware-initialization will be done here */
              ....
              return 0;
      }

      /* close callback */
      static int snd_mychip_playback_close(struct snd_pcm_substream *substream)
      {
              struct mychip *chip = snd_pcm_substream_chip(substream);
              /* the hardware-specific codes will be here */
              ....
              return 0;

      }

      /* open callback */
      static int snd_mychip_capture_open(struct snd_pcm_substream *substream)
      {
              struct mychip *chip = snd_pcm_substream_chip(substream);
              struct snd_pcm_runtime *runtime = substream->runtime;

              runtime->hw = snd_mychip_capture_hw;
              /* more hardware-initialization will be done here */
              ....
              return 0;
      }

      /* close callback */
      static int snd_mychip_capture_close(struct snd_pcm_substream *substream)
      {
              struct mychip *chip = snd_pcm_substream_chip(substream);
              /* the hardware-specific codes will be here */
              ....
              return 0;
      }

      /* hw_params callback */
      static int snd_mychip_pcm_hw_params(struct snd_pcm_substream *substream,
                                   struct snd_pcm_hw_params *hw_params)
      {
              /* the hardware-specific codes will be here */
              ....
              return 0;
      }

      /* hw_free callback */
      static int snd_mychip_pcm_hw_free(struct snd_pcm_substream *substream)
      {
              /* the hardware-specific codes will be here */
              ....
              return 0;
      }

      /* prepare callback */
      static int snd_mychip_pcm_prepare(struct snd_pcm_substream *substream)
      {
              struct mychip *chip = snd_pcm_substream_chip(substream);
              struct snd_pcm_runtime *runtime = substream->runtime;

              /* set up the hardware with the current configuration
               * for example...
               */
              mychip_set_sample_format(chip, runtime->format);
              mychip_set_sample_rate(chip, runtime->rate);
              mychip_set_channels(chip, runtime->channels);
              mychip_set_dma_setup(chip, runtime->dma_addr,
                                   chip->buffer_size,
                                   chip->period_size);
              return 0;
      }

      /* trigger callback */
      static int snd_mychip_pcm_trigger(struct snd_pcm_substream *substream,
                                        int cmd)
      {
              switch (cmd) {
              case SNDRV_PCM_TRIGGER_START:
                      /* do something to start the PCM engine */
                      ....
                      break;
              case SNDRV_PCM_TRIGGER_STOP:
                      /* do something to stop the PCM engine */
                      ....
                      break;
              default:
                      return -EINVAL;
              }
      }

      /* pointer callback */
      static snd_pcm_uframes_t
      snd_mychip_pcm_pointer(struct snd_pcm_substream *substream)
      {
              struct mychip *chip = snd_pcm_substream_chip(substream);
              unsigned int current_ptr;

              /* get the current hardware pointer */
              current_ptr = mychip_get_hw_pointer(chip);
              return current_ptr;
      }

      /* operators */
      static struct snd_pcm_ops snd_mychip_playback_ops = {
              .open =        snd_mychip_playback_open,
              .close =       snd_mychip_playback_close,
              .hw_params =   snd_mychip_pcm_hw_params,
              .hw_free =     snd_mychip_pcm_hw_free,
              .prepare =     snd_mychip_pcm_prepare,
              .trigger =     snd_mychip_pcm_trigger,
              .pointer =     snd_mychip_pcm_pointer,
      };

      /* operators */
      static struct snd_pcm_ops snd_mychip_capture_ops = {
              .open =        snd_mychip_capture_open,
              .close =       snd_mychip_capture_close,
              .hw_params =   snd_mychip_pcm_hw_params,
              .hw_free =     snd_mychip_pcm_hw_free,
              .prepare =     snd_mychip_pcm_prepare,
              .trigger =     snd_mychip_pcm_trigger,
              .pointer =     snd_mychip_pcm_pointer,
      };

      /*
       *  definitions of capture are omitted here...
       */

      /* create a pcm device */
      static int snd_mychip_new_pcm(struct mychip *chip)
      {
              struct snd_pcm *pcm;
              int err;

              err = snd_pcm_new(chip->card, "My Chip", 0, 1, 1, &pcm);
              if (err < 0)
                      return err;
              pcm->private_data = chip;
              strcpy(pcm->name, "My Chip");
              chip->pcm = pcm;
              /* set operators */
              snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
                              &snd_mychip_playback_ops);
              snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
                              &snd_mychip_capture_ops);
              /* pre-allocation of buffers */
              /* NOTE: this may fail */
              snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
                                             &chip->pci->dev,
                                             64*1024, 64*1024);
              return 0;
      }


PCM Constructor
---------------

A PCM instance is allocated by the :c:func:`snd_pcm_new()`
function. It would be better to create a constructor for the PCM, namely::

  static int snd_mychip_new_pcm(struct mychip *chip)
  {
          struct snd_pcm *pcm;
          int err;

          err = snd_pcm_new(chip->card, "My Chip", 0, 1, 1, &pcm);
          if (err < 0) 
                  return err;
          pcm->private_data = chip;
          strcpy(pcm->name, "My Chip");
          chip->pcm = pcm;
          ...
          return 0;
  }

The :c:func:`snd_pcm_new()` function takes six arguments. The
first argument is the card pointer to which this PCM is assigned, and
the second is the ID string.

The third argument (``index``, 0 in the above) is the index of this new
PCM. It begins from zero. If you create more than one PCM instances,
specify the different numbers in this argument. For example, ``index =
1`` for the second PCM device.

The fourth and fifth arguments are the number of substreams for playback
and capture, respectively. Here 1 is used for both arguments. When no
playback or capture substreams are available, pass 0 to the
corresponding argument.

If a chip supports multiple playbacks or captures, you can specify more
numbers, but they must be handled properly in open/close, etc.
callbacks. When you need to know which substream you are referring to,
then it can be obtained from struct snd_pcm_substream data passed to each
callback as follows::

  struct snd_pcm_substream *substream;
  int index = substream->number;


After the PCM is created, you need to set operators for each PCM stream::

  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
                  &snd_mychip_playback_ops);
  snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
                  &snd_mychip_capture_ops);

The operators are defined typically like this::

  static struct snd_pcm_ops snd_mychip_playback_ops = {
          .open =        snd_mychip_pcm_open,
          .close =       snd_mychip_pcm_close,
          .hw_params =   snd_mychip_pcm_hw_params,
          .hw_free =     snd_mychip_pcm_hw_free,
          .prepare =     snd_mychip_pcm_prepare,
          .trigger =     snd_mychip_pcm_trigger,
          .pointer =     snd_mychip_pcm_pointer,
  };

All the callbacks are described in the Operators_ subsection.

After setting the operators, you probably will want to pre-allocate the
buffer and set up the managed allocation mode.
For that, simply call the following::

  snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
                                 &chip->pci->dev,
                                 64*1024, 64*1024);

It will allocate a buffer up to 64kB by default. Buffer management
details will be described in the later section `Buffer and Memory
Management`_.

Additionally, you can set some extra information for this PCM in
``pcm->info_flags``. The available values are defined as
``SNDRV_PCM_INFO_XXX`` in ``<sound/asound.h>``, which is used for the
hardware definition (described later). When your soundchip supports only
half-duplex, specify it like this::

  pcm->info_flags = SNDRV_PCM_INFO_HALF_DUPLEX;


... And the Destructor?
-----------------------

The destructor for a PCM instance is not always necessary. Since the PCM
device will be released by the middle layer code automatically, you
don't have to call the destructor explicitly.

The destructor would be necessary if you created special records
internally and needed to release them. In such a case, set the
destructor function to ``pcm->private_free``::

      static void mychip_pcm_free(struct snd_pcm *pcm)
      {
              struct mychip *chip = snd_pcm_chip(pcm);
              /* free your own data */
              kfree(chip->my_private_pcm_data);
              /* do what you like else */
              ....
      }

      static int snd_mychip_new_pcm(struct mychip *chip)
      {
              struct snd_pcm *pcm;
              ....
              /* allocate your own data */
              chip->my_private_pcm_data = kmalloc(...);
              /* set the destructor */
              pcm->private_data = chip;
              pcm->private_free = mychip_pcm_free;
              ....
      }



Runtime Pointer - The Chest of PCM Information
----------------------------------------------

When the PCM substream is opened, a PCM runtime instance is allocated
and assigned to the substream. This pointer is accessible via
``substream->runtime``. This runtime pointer holds most information you
need to control the PCM: a copy of hw_params and sw_params
configurations, the buffer pointers, mmap records, spinlocks, etc.

The definition of runtime instance is found in ``<sound/pcm.h>``. Here
is the relevant part of this file::

  struct _snd_pcm_runtime {
          /* -- Status -- */
          struct snd_pcm_substream *trigger_master;
          snd_timestamp_t trigger_tstamp;	/* trigger timestamp */
          int overrange;
          snd_pcm_uframes_t avail_max;
          snd_pcm_uframes_t hw_ptr_base;	/* Position at buffer restart */
          snd_pcm_uframes_t hw_ptr_interrupt; /* Position at interrupt time*/
  
          /* -- HW params -- */
          snd_pcm_access_t access;	/* access mode */
          snd_pcm_format_t format;	/* SNDRV_PCM_FORMAT_* */
          snd_pcm_subformat_t subformat;	/* subformat */
          unsigned int rate;		/* rate in Hz */
          unsigned int channels;		/* channels */
          snd_pcm_uframes_t period_size;	/* period size */
          unsigned int periods;		/* periods */
          snd_pcm_uframes_t buffer_size;	/* buffer size */
          unsigned int tick_time;		/* tick time */
          snd_pcm_uframes_t min_align;	/* Min alignment for the format */
          size_t byte_align;
          unsigned int frame_bits;
          unsigned int sample_bits;
          unsigned int info;
          unsigned int rate_num;
          unsigned int rate_den;
  
          /* -- SW params -- */
          struct timespec tstamp_mode;	/* mmap timestamp is updated */
          unsigned int period_step;
          unsigned int sleep_min;		/* min ticks to sleep */
          snd_pcm_uframes_t start_threshold;
          /*
           * The following two thresholds alleviate playback buffer underruns; when
           * hw_avail drops below the threshold, the respective action is triggered:
           */
          snd_pcm_uframes_t stop_threshold;	/* - stop playback */
          snd_pcm_uframes_t silence_threshold;	/* - pre-fill buffer with silence */
          snd_pcm_uframes_t silence_size;       /* max size of silence pre-fill; when >= boundary,
                                                 * fill played area with silence immediately */
          snd_pcm_uframes_t boundary;	/* pointers wrap point */
  
          /* internal data of auto-silencer */
          snd_pcm_uframes_t silence_start; /* starting pointer to silence area */
          snd_pcm_uframes_t silence_filled; /* size filled with silence */
  
          snd_pcm_sync_id_t sync;		/* hardware synchronization ID */
  
          /* -- mmap -- */
          volatile struct snd_pcm_mmap_status *status;
          volatile struct snd_pcm_mmap_control *control;
          atomic_t mmap_count;
  
          /* -- locking / scheduling -- */
          spinlock_t lock;
          wait_queue_head_t sleep;
          struct timer_list tick_timer;
          struct fasync_struct *fasync;

          /* -- private section -- */
          void *private_data;
          void (*private_free)(struct snd_pcm_runtime *runtime);
  
          /* -- hardware description -- */
          struct snd_pcm_hardware hw;
          struct snd_pcm_hw_constraints hw_constraints;
  
          /* -- timer -- */
          unsigned int timer_resolution;	/* timer resolution */
  
          /* -- DMA -- */           
          unsigned char *dma_area;	/* DMA area */
          dma_addr_t dma_addr;		/* physical bus address (not accessible from main CPU) */
          size_t dma_bytes;		/* size of DMA area */
  
          struct snd_dma_buffer *dma_buffer_p;	/* allocated buffer */
  
  #if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
          /* -- OSS things -- */
          struct snd_pcm_oss_runtime oss;
  #endif
  };


For the operators (callbacks) of each sound driver, most of these
records are supposed to be read-only. Only the PCM middle-layer changes
/ updates them. The exceptions are the hardware description (hw) DMA
buffer information and the private data. Besides, if you use the
standard managed buffer allocation mode, you don't need to set the
DMA buffer information by yourself.

In the sections below, important records are explained.

Hardware Description
~~~~~~~~~~~~~~~~~~~~

The hardware descriptor (struct snd_pcm_hardware) contains the definitions of
the fundamental hardware configuration. Above all, you'll need to define this
in the `PCM open callback`_. Note that the runtime instance holds a copy of
the descriptor, not a pointer to the existing descriptor. That is,
in the open callback, you can modify the copied descriptor
(``runtime->hw``) as you need. For example, if the maximum number of
channels is 1 only on some chip models, you can still use the same
hardware descriptor and change the channels_max later::

          struct snd_pcm_runtime *runtime = substream->runtime;
          ...
          runtime->hw = snd_mychip_playback_hw; /* common definition */
          if (chip->model == VERY_OLD_ONE)
                  runtime->hw.channels_max = 1;

Typically, you'll have a hardware descriptor as below::

  static struct snd_pcm_hardware snd_mychip_playback_hw = {
          .info = (SNDRV_PCM_INFO_MMAP |
                   SNDRV_PCM_INFO_INTERLEAVED |
                   SNDRV_PCM_INFO_BLOCK_TRANSFER |
                   SNDRV_PCM_INFO_MMAP_VALID),
          .formats =          SNDRV_PCM_FMTBIT_S16_LE,
          .rates =            SNDRV_PCM_RATE_8000_48000,
          .rate_min =         8000,
          .rate_max =         48000,
          .channels_min =     2,
          .channels_max =     2,
          .buffer_bytes_max = 32768,
          .period_bytes_min = 4096,
          .period_bytes_max = 32768,
          .periods_min =      1,
          .periods_max =      1024,
  };

-  The ``info`` field contains the type and capabilities of this
   PCM. The bit flags are defined in ``<sound/asound.h>`` as
   ``SNDRV_PCM_INFO_XXX``. Here, at least, you have to specify whether
   mmap is supported and which interleaving formats are
   supported. When the hardware supports mmap, add the
   ``SNDRV_PCM_INFO_MMAP`` flag here. When the hardware supports the
   interleaved or the non-interleaved formats, the
   ``SNDRV_PCM_INFO_INTERLEAVED`` or ``SNDRV_PCM_INFO_NONINTERLEAVED``
   flag must be set, respectively. If both are supported, you can set
   both, too.

   In the above example, ``MMAP_VALID`` and ``BLOCK_TRANSFER`` are
   specified for the OSS mmap mode. Usually both are set. Of course,
   ``MMAP_VALID`` is set only if mmap is really supported.

   The other possible flags are ``SNDRV_PCM_INFO_PAUSE`` and
   ``SNDRV_PCM_INFO_RESUME``. The ``PAUSE`` bit means that the PCM
   supports the “pause” operation, while the ``RESUME`` bit means that
   the PCM supports the full “suspend/resume” operation. If the
   ``PAUSE`` flag is set, the ``trigger`` callback below must handle
   the corresponding (pause push/release) commands. The suspend/resume
   trigger commands can be defined even without the ``RESUME``
   flag. See the `Power Management`_ section for details.

   When the PCM substreams can be synchronized (typically,
   synchronized start/stop of a playback and a capture stream), you
   can give ``SNDRV_PCM_INFO_SYNC_START``, too. In this case, you'll
   need to check the linked-list of PCM substreams in the trigger
   callback. This will be described in a later section.

-  The ``formats`` field contains the bit-flags of supported formats
   (``SNDRV_PCM_FMTBIT_XXX``). If the hardware supports more than one
   format, give all or'ed bits. In the example above, the signed 16bit
   little-endian format is specified.

-  The ``rates`` field contains the bit-flags of supported rates
   (``SNDRV_PCM_RATE_XXX``). When the chip supports continuous rates,
   pass the ``CONTINUOUS`` bit additionally. The pre-defined rate bits
   are provided only for typical rates. If your chip supports
   unconventional rates, you need to add the ``KNOT`` bit and set up
   the hardware constraint manually (explained later).

-  ``rate_min`` and ``rate_max`` define the minimum and maximum sample
   rate. This should correspond somehow to ``rates`` bits.

-  ``channels_min`` and ``channels_max`` define, as you might have already
   expected, the minimum and maximum number of channels.

-  ``buffer_bytes_max`` defines the maximum buffer size in
   bytes. There is no ``buffer_bytes_min`` field, since it can be
   calculated from the minimum period size and the minimum number of
   periods. Meanwhile, ``period_bytes_min`` and ``period_bytes_max``
   define the minimum and maximum size of the period in bytes.
   ``periods_max`` and ``periods_min`` define the maximum and minimum
   number of periods in the buffer.

   The “period” is a term that corresponds to a fragment in the OSS
   world. The period defines the point at which a PCM interrupt is
   generated. This point strongly depends on the hardware. Generally,
   a smaller period size will give you more interrupts, which results
   in being able to fill/drain the buffer more timely. In the case of
   capture, this size defines the input latency. On the other hand,
   the whole buffer size defines the output latency for the playback
   direction.

-  There is also a field ``fifo_size``. This specifies the size of the
   hardware FIFO, but currently it is neither used by the drivers nor
   in the alsa-lib. So, you can ignore this field.

PCM Configurations
~~~~~~~~~~~~~~~~~~

Ok, let's go back again to the PCM runtime records. The most
frequently referred records in the runtime instance are the PCM
configurations. The PCM configurations are stored in the runtime
instance after the application sends ``hw_params`` data via
alsa-lib. There are many fields copied from hw_params and sw_params
structs. For example, ``format`` holds the format type chosen by the
application. This field contains the enum value
``SNDRV_PCM_FORMAT_XXX``.

One thing to be noted is that the configured buffer and period sizes
are stored in “frames” in the runtime. In the ALSA world, ``1 frame =
channels \* samples-size``. For conversion between frames and bytes,
you can use the :c:func:`frames_to_bytes()` and
:c:func:`bytes_to_frames()` helper functions::

  period_bytes = frames_to_bytes(runtime, runtime->period_size);

Also, many software parameters (sw_params) are stored in frames, too.
Please check the type of the field. ``snd_pcm_uframes_t`` is for
frames as unsigned integer while ``snd_pcm_sframes_t`` is for
frames as signed integer.

DMA Buffer Information
~~~~~~~~~~~~~~~~~~~~~~

The DMA buffer is defined by the following four fields: ``dma_area``,
``dma_addr``, ``dma_bytes`` and ``dma_private``. ``dma_area``
holds the buffer pointer (the logical address). You can call
:c:func:`memcpy()` from/to this pointer. Meanwhile, ``dma_addr`` holds
the physical address of the buffer. This field is specified only when
the buffer is a linear buffer. ``dma_bytes`` holds the size of the
buffer in bytes. ``dma_private`` is used for the ALSA DMA allocator.

If you use either the managed buffer allocation mode or the standard
API function :c:func:`snd_pcm_lib_malloc_pages()` for allocating the buffer,
these fields are set by the ALSA middle layer, and you should *not*
change them by yourself. You can read them but not write them. On the
other hand, if you want to allocate the buffer by yourself, you'll
need to manage it in the hw_params callback. At least, ``dma_bytes`` is
mandatory. ``dma_area`` is necessary when the buffer is mmapped. If
your driver doesn't support mmap, this field is not
necessary. ``dma_addr`` is also optional. You can use dma_private as
you like, too.

Running Status
~~~~~~~~~~~~~~

The running status can be referred via ``runtime->status``. This is
a pointer to a struct snd_pcm_mmap_status record.
For example, you can get the current
DMA hardware pointer via ``runtime->status->hw_ptr``.

The DMA application pointer can be referred via ``runtime->control``,
which points to a struct snd_pcm_mmap_control record.
However, accessing this value directly is not recommended.

Private Data
~~~~~~~~~~~~

You can allocate a record for the substream and store it in
``runtime->private_data``. Usually, this is done in the `PCM open
callback`_. Don't mix this with ``pcm->private_data``. The
``pcm->private_data`` usually points to the chip instance assigned
statically at creation time of the PCM device, while
``runtime->private_data``
points to a dynamic data structure created in the PCM open
callback::

  static int snd_xxx_open(struct snd_pcm_substream *substream)
  {
          struct my_pcm_data *data;
          ....
          data = kmalloc(sizeof(*data), GFP_KERNEL);
          substream->runtime->private_data = data;
          ....
  }


The allocated object must be released in the `close callback`_.

Operators
---------

OK, now let me give details about each PCM callback (``ops``). In
general, every callback must return 0 if successful, or a negative
error number such as ``-EINVAL``. To choose an appropriate error
number, it is advised to check what value other parts of the kernel
return when the same kind of request fails.

Each callback function takes at least one argument containing a
struct snd_pcm_substream pointer. To retrieve the chip
record from the given substream instance, you can use the following
macro::

  int xxx(...) {
          struct mychip *chip = snd_pcm_substream_chip(substream);
          ....
  }

The macro reads ``substream->private_data``, which is a copy of
``pcm->private_data``. You can override the former if you need to
assign different data records per PCM substream. For example, the
cmi8330 driver assigns different ``private_data`` for playback and
capture directions, because it uses two different codecs (SB- and
AD-compatible) for different directions.

PCM open callback
~~~~~~~~~~~~~~~~~

::

  static int snd_xxx_open(struct snd_pcm_substream *substream);

This is called when a PCM substream is opened.

At least, here you have to initialize the ``runtime->hw``
record. Typically, this is done like this::

  static int snd_xxx_open(struct snd_pcm_substream *substream)
  {
          struct mychip *chip = snd_pcm_substream_chip(substream);
          struct snd_pcm_runtime *runtime = substream->runtime;

          runtime->hw = snd_mychip_playback_hw;
          return 0;
  }

where ``snd_mychip_playback_hw`` is the pre-defined hardware
description.

You can allocate private data in this callback, as described in the
`Private Data`_ section.

If the hardware configuration needs more constraints, set the hardware
constraints here, too. See Constraints_ for more details.

close callback
~~~~~~~~~~~~~~

::

  static int snd_xxx_close(struct snd_pcm_substream *substream);


Obviously, this is called when a PCM substream is closed.

Any private instance for a PCM substream allocated in the ``open``
callback will be released here::

  static int snd_xxx_close(struct snd_pcm_substream *substream)
  {
          ....
          kfree(substream->runtime->private_data);
          ....
  }

ioctl callback
~~~~~~~~~~~~~~

This is used for any special call to PCM ioctls. But usually you can
leave it NULL, then the PCM core calls the generic ioctl callback
function :c:func:`snd_pcm_lib_ioctl()`.  If you need to deal with a
unique setup of channel info or reset procedure, you can pass your own
callback function here.

hw_params callback
~~~~~~~~~~~~~~~~~~~

::

  static int snd_xxx_hw_params(struct snd_pcm_substream *substream,
                               struct snd_pcm_hw_params *hw_params);

This is called when the hardware parameters (``hw_params``) are set up
by the application, that is, once when the buffer size, the period
size, the format, etc. are defined for the PCM substream.

Many hardware setups should be done in this callback, including the
allocation of buffers.

Parameters to be initialized are retrieved by the
:c:func:`params_xxx()` macros.

When you choose managed buffer allocation mode for the substream,
a buffer is already allocated before this callback gets
called. Alternatively, you can call a helper function below for
allocating the buffer::

  snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));

:c:func:`snd_pcm_lib_malloc_pages()` is available only when the
DMA buffers have been pre-allocated. See the section `Buffer Types`_
for more details.

Note that this one and the ``prepare`` callback may be called multiple
times per initialization. For example, the OSS emulation may call these
callbacks at each change via its ioctl.

Thus, you need to be careful not to allocate the same buffers many
times, which will lead to memory leaks! Calling the helper function
above many times is OK. It will release the previous buffer
automatically when it was already allocated.

Another note is that this callback is non-atomic (schedulable) by
default, i.e. when no ``nonatomic`` flag set. This is important,
because the ``trigger`` callback is atomic (non-schedulable). That is,
mutexes or any schedule-related functions are not available in the
``trigger`` callback. Please see the subsection Atomicity_ for
details.

hw_free callback
~~~~~~~~~~~~~~~~~

::

  static int snd_xxx_hw_free(struct snd_pcm_substream *substream);

This is called to release the resources allocated via
``hw_params``.

This function is always called before the close callback is called.
Also, the callback may be called multiple times, too. Keep track
whether each resource was already released.

When you have chosen managed buffer allocation mode for the PCM
substream, the allocated PCM buffer will be automatically released
after this callback gets called.  Otherwise you'll have to release the
buffer manually.  Typically, when the buffer was allocated from the
pre-allocated pool, you can use the standard API function
:c:func:`snd_pcm_lib_malloc_pages()` like::

  snd_pcm_lib_free_pages(substream);

prepare callback
~~~~~~~~~~~~~~~~

::

  static int snd_xxx_prepare(struct snd_pcm_substream *substream);

This callback is called when the PCM is “prepared”. You can set the
format type, sample rate, etc. here. The difference from ``hw_params``
is that the ``prepare`` callback will be called each time
:c:func:`snd_pcm_prepare()` is called, i.e. when recovering after
underruns, etc.

Note that this callback is non-atomic. You can use
schedule-related functions safely in this callback.

In this and the following callbacks, you can refer to the values via
the runtime record, ``substream->runtime``. For example, to get the
current rate, format or channels, access to ``runtime->rate``,
``runtime->format`` or ``runtime->channels``, respectively. The
physical address of the allocated buffer is set to
``runtime->dma_area``. The buffer and period sizes are in
``runtime->buffer_size`` and ``runtime->period_size``, respectively.

Be careful that this callback will be called many times at each setup,
too.

trigger callback
~~~~~~~~~~~~~~~~

::

  static int snd_xxx_trigger(struct snd_pcm_substream *substream, int cmd);

This is called when the PCM is started, stopped or paused.

The action is specified in the second argument, ``SNDRV_PCM_TRIGGER_XXX``
defined in ``<sound/pcm.h>``. At least, the ``START``
and ``STOP`` commands must be defined in this callback::

  switch (cmd) {
  case SNDRV_PCM_TRIGGER_START:
          /* do something to start the PCM engine */
          break;
  case SNDRV_PCM_TRIGGER_STOP:
          /* do something to stop the PCM engine */
          break;
  default:
          return -EINVAL;
  }

When the PCM supports the pause operation (given in the info field of
the hardware table), the ``PAUSE_PUSH`` and ``PAUSE_RELEASE`` commands
must be handled here, too. The former is the command to pause the PCM,
and the latter to restart the PCM again.

When the PCM supports the suspend/resume operation, regardless of full
or partial suspend/resume support, the ``SUSPEND`` and ``RESUME``
commands must be handled, too. These commands are issued when the
power-management status is changed. Obviously, the ``SUSPEND`` and
``RESUME`` commands suspend and resume the PCM substream, and usually,
they are identical to the ``STOP`` and ``START`` commands, respectively.
See the `Power Management`_ section for details.

As mentioned, this callback is atomic by default unless the ``nonatomic``
flag set, and you cannot call functions which may sleep. The
``trigger`` callback should be as minimal as possible, just really
triggering the DMA. The other stuff should be initialized in
``hw_params`` and ``prepare`` callbacks properly beforehand.

sync_stop callback
~~~~~~~~~~~~~~~~~~

::

  static int snd_xxx_sync_stop(struct snd_pcm_substream *substream);

This callback is optional, and NULL can be passed.  It's called after
the PCM core stops the stream, before it changes the stream state via
``prepare``, ``hw_params`` or ``hw_free``.
Since the IRQ handler might be still pending, we need to wait until
the pending task finishes before moving to the next step; otherwise it
might lead to a crash due to resource conflicts or access to freed
resources.  A typical behavior is to call a synchronization function
like :c:func:`synchronize_irq()` here.

For the majority of drivers that need only a call of
:c:func:`synchronize_irq()`, there is a simpler setup, too.
While keeping the ``sync_stop`` PCM callback NULL, the driver can set
the ``card->sync_irq`` field to the returned interrupt number after
requesting an IRQ, instead.   Then PCM core will call
:c:func:`synchronize_irq()` with the given IRQ appropriately.

If the IRQ handler is released by the card destructor, you don't need
to clear ``card->sync_irq``, as the card itself is being released.
So, usually you'll need to add just a single line for assigning
``card->sync_irq`` in the driver code unless the driver re-acquires
the IRQ.  When the driver frees and re-acquires the IRQ dynamically
(e.g. for suspend/resume), it needs to clear and re-set
``card->sync_irq`` again appropriately.

pointer callback
~~~~~~~~~~~~~~~~

::

  static snd_pcm_uframes_t snd_xxx_pointer(struct snd_pcm_substream *substream)

This callback is called when the PCM middle layer inquires the current
hardware position in the buffer. The position must be returned in
frames, ranging from 0 to ``buffer_size - 1``. 

This is usually called from the buffer-update routine in the PCM
middle layer, which is invoked when :c:func:`snd_pcm_period_elapsed()`
is called by the interrupt routine. Then the PCM middle layer updates
the position and calculates the available space, and wakes up the
sleeping poll threads, etc.

This callback is also atomic by default.

copy_user, copy_kernel and fill_silence ops
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

These callbacks are not mandatory, and can be omitted in most cases.
These callbacks are used when the hardware buffer cannot be in the
normal memory space. Some chips have their own buffer in the hardware
which is not mappable. In such a case, you have to transfer the data
manually from the memory buffer to the hardware buffer. Or, if the
buffer is non-contiguous on both physical and virtual memory spaces,
these callbacks must be defined, too.

If these two callbacks are defined, copy and set-silence operations
are done by them. The details will be described in the later section
`Buffer and Memory Management`_.

ack callback
~~~~~~~~~~~~

This callback is also not mandatory. This callback is called when the
``appl_ptr`` is updated in read or write operations. Some drivers like
emu10k1-fx and cs46xx need to track the current ``appl_ptr`` for the
internal buffer, and this callback is useful only for such a purpose.

The callback function may return 0 or a negative error. When the
return value is ``-EPIPE``, PCM core treats that as a buffer XRUN,
and changes the state to ``SNDRV_PCM_STATE_XRUN`` automatically.

This callback is atomic by default.

page callback
~~~~~~~~~~~~~

This callback is optional too. The mmap calls this callback to get the
page fault address.

You need no special callback for the standard SG-buffer or vmalloc-
buffer. Hence this callback should be rarely used.

mmap callback
~~~~~~~~~~~~~

This is another optional callback for controlling mmap behavior.
When defined, the PCM core calls this callback when a page is
memory-mapped, instead of using the standard helper.
If you need special handling (due to some architecture or
device-specific issues), implement everything here as you like.


PCM Interrupt Handler
---------------------

The remainder of the PCM stuff is the PCM interrupt handler. The role
of the PCM
interrupt handler in the sound driver is to update the buffer position
and to tell the PCM middle layer when the buffer position goes across
the specified period boundary. To inform about this, call the
:c:func:`snd_pcm_period_elapsed()` function.

There are several ways sound chips can generate interrupts.

Interrupts at the period (fragment) boundary
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This is the most frequently found type: the hardware generates an
interrupt at each period boundary. In this case, you can call
:c:func:`snd_pcm_period_elapsed()` at each interrupt.

:c:func:`snd_pcm_period_elapsed()` takes the substream pointer as
its argument. Thus, you need to keep the substream pointer accessible
from the chip instance. For example, define ``substream`` field in the
chip record to hold the current running substream pointer, and set the
pointer value at ``open`` callback (and reset at ``close`` callback).

If you acquire a spinlock in the interrupt handler, and the lock is used
in other PCM callbacks, too, then you have to release the lock before
calling :c:func:`snd_pcm_period_elapsed()`, because
:c:func:`snd_pcm_period_elapsed()` calls other PCM callbacks
inside.

Typical code would look like::


      static irqreturn_t snd_mychip_interrupt(int irq, void *dev_id)
      {
              struct mychip *chip = dev_id;
              spin_lock(&chip->lock);
              ....
              if (pcm_irq_invoked(chip)) {
                      /* call updater, unlock before it */
                      spin_unlock(&chip->lock);
                      snd_pcm_period_elapsed(chip->substream);
                      spin_lock(&chip->lock);
                      /* acknowledge the interrupt if necessary */
              }
              ....
              spin_unlock(&chip->lock);
              return IRQ_HANDLED;
      }

Also, when the device can detect a buffer underrun/overrun, the driver
can notify the XRUN status to the PCM core by calling
:c:func:`snd_pcm_stop_xrun()`. This function stops the stream and sets
the PCM state to ``SNDRV_PCM_STATE_XRUN``. Note that it must be called
outside the PCM stream lock, hence it can't be called from the atomic
callback.


High frequency timer interrupts
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This happens when the hardware doesn't generate interrupts at the period
boundary but issues timer interrupts at a fixed timer rate (e.g. es1968
or ymfpci drivers). In this case, you need to check the current hardware
position and accumulate the processed sample length at each interrupt.
When the accumulated size exceeds the period size, call
:c:func:`snd_pcm_period_elapsed()` and reset the accumulator.

Typical code would look as follows::


      static irqreturn_t snd_mychip_interrupt(int irq, void *dev_id)
      {
              struct mychip *chip = dev_id;
              spin_lock(&chip->lock);
              ....
              if (pcm_irq_invoked(chip)) {
                      unsigned int last_ptr, size;
                      /* get the current hardware pointer (in frames) */
                      last_ptr = get_hw_ptr(chip);
                      /* calculate the processed frames since the
                       * last update
                       */
                      if (last_ptr < chip->last_ptr)
                              size = runtime->buffer_size + last_ptr
                                       - chip->last_ptr;
                      else
                              size = last_ptr - chip->last_ptr;
                      /* remember the last updated point */
                      chip->last_ptr = last_ptr;
                      /* accumulate the size */
                      chip->size += size;
                      /* over the period boundary? */
                      if (chip->size >= runtime->period_size) {
                              /* reset the accumulator */
                              chip->size %= runtime->period_size;
                              /* call updater */
                              spin_unlock(&chip->lock);
                              snd_pcm_period_elapsed(substream);
                              spin_lock(&chip->lock);
                      }
                      /* acknowledge the interrupt if necessary */
              }
              ....
              spin_unlock(&chip->lock);
              return IRQ_HANDLED;
      }



On calling :c:func:`snd_pcm_period_elapsed()`
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In both cases, even if more than one period has elapsed, you don't have
to call :c:func:`snd_pcm_period_elapsed()` many times. Call only
once. And the PCM layer will check the current hardware pointer and
update to the latest status.

Atomicity
---------

One of the most important (and thus difficult to debug) problems in
kernel programming are race conditions. In the Linux kernel, they are
usually avoided via spin-locks, mutexes or semaphores. In general, if a
race condition can happen in an interrupt handler, it has to be managed
atomically, and you have to use a spinlock to protect the critical
section. If the critical section is not in interrupt handler code and if
taking a relatively long time to execute is acceptable, you should use
mutexes or semaphores instead.

As already seen, some PCM callbacks are atomic and some are not. For
example, the ``hw_params`` callback is non-atomic, while the ``trigger``
callback is atomic. This means, the latter is called already in a
spinlock held by the PCM middle layer, the PCM stream lock. Please
take this atomicity into account when you choose a locking scheme in
the callbacks.

In the atomic callbacks, you cannot use functions which may call
:c:func:`schedule()` or go to :c:func:`sleep()`. Semaphores and
mutexes can sleep, and hence they cannot be used inside the atomic
callbacks (e.g. ``trigger`` callback). To implement some delay in such a
callback, please use :c:func:`udelay()` or :c:func:`mdelay()`.

All three atomic callbacks (trigger, pointer, and ack) are called with
local interrupts disabled.

However, it is possible to request all PCM operations to be non-atomic.
This assumes that all call sites are in
non-atomic contexts. For example, the function
:c:func:`snd_pcm_period_elapsed()` is called typically from the
interrupt handler. But, if you set up the driver to use a threaded
interrupt handler, this call can be in non-atomic context, too. In such
a case, you can set the ``nonatomic`` field of the struct snd_pcm object
after creating it. When this flag is set, mutex and rwsem are used internally
in the PCM core instead of spin and rwlocks, so that you can call all PCM
functions safely in a non-atomic
context.

Also, in some cases, you might need to call
:c:func:`snd_pcm_period_elapsed()` in the atomic context (e.g. the
period gets elapsed during ``ack`` or other callback). There is a
variant that can be called inside the PCM stream lock
:c:func:`snd_pcm_period_elapsed_under_stream_lock()` for that purpose,
too.

Constraints
-----------

Due to physical limitations, hardware is not infinitely configurable.
These limitations are expressed by setting constraints.

For example, in order to restrict the sample rates to some supported
values, use :c:func:`snd_pcm_hw_constraint_list()`. You need to
call this function in the open callback::

      static unsigned int rates[] =
              {4000, 10000, 22050, 44100};
      static struct snd_pcm_hw_constraint_list constraints_rates = {
              .count = ARRAY_SIZE(rates),
              .list = rates,
              .mask = 0,
      };

      static int snd_mychip_pcm_open(struct snd_pcm_substream *substream)
      {
              int err;
              ....
              err = snd_pcm_hw_constraint_list(substream->runtime, 0,
                                               SNDRV_PCM_HW_PARAM_RATE,
                                               &constraints_rates);
              if (err < 0)
                      return err;
              ....
      }

There are many different constraints. Look at ``sound/pcm.h`` for a
complete list. You can even define your own constraint rules. For
example, let's suppose my_chip can manage a substream of 1 channel if
and only if the format is ``S16_LE``, otherwise it supports any format
specified in struct snd_pcm_hardware (or in any other
constraint_list). You can build a rule like this::

      static int hw_rule_channels_by_format(struct snd_pcm_hw_params *params,
                                            struct snd_pcm_hw_rule *rule)
      {
              struct snd_interval *c = hw_param_interval(params,
                            SNDRV_PCM_HW_PARAM_CHANNELS);
              struct snd_mask *f = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
              struct snd_interval ch;

              snd_interval_any(&ch);
              if (f->bits[0] == SNDRV_PCM_FMTBIT_S16_LE) {
                      ch.min = ch.max = 1;
                      ch.integer = 1;
                      return snd_interval_refine(c, &ch);
              }
              return 0;
      }


Then you need to call this function to add your rule::

  snd_pcm_hw_rule_add(substream->runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
                      hw_rule_channels_by_format, NULL,
                      SNDRV_PCM_HW_PARAM_FORMAT, -1);

The rule function is called when an application sets the PCM format, and
it refines the number of channels accordingly. But an application may
set the number of channels before setting the format. Thus you also need
to define the inverse rule::

      static int hw_rule_format_by_channels(struct snd_pcm_hw_params *params,
                                            struct snd_pcm_hw_rule *rule)
      {
              struct snd_interval *c = hw_param_interval(params,
                    SNDRV_PCM_HW_PARAM_CHANNELS);
              struct snd_mask *f = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
              struct snd_mask fmt;

              snd_mask_any(&fmt);    /* Init the struct */
              if (c->min < 2) {
                      fmt.bits[0] &= SNDRV_PCM_FMTBIT_S16_LE;
                      return snd_mask_refine(f, &fmt);
              }
              return 0;
      }


... and in the open callback::

  snd_pcm_hw_rule_add(substream->runtime, 0, SNDRV_PCM_HW_PARAM_FORMAT,
                      hw_rule_format_by_channels, NULL,
                      SNDRV_PCM_HW_PARAM_CHANNELS, -1);

One typical usage of the hw constraints is to align the buffer size
with the period size.  By default, ALSA PCM core doesn't enforce the
buffer size to be aligned with the period size.  For example, it'd be
possible to have a combination like 256 period bytes with 999 buffer
bytes.

Many device chips, however, require the buffer to be a multiple of
periods.  In such a case, call
:c:func:`snd_pcm_hw_constraint_integer()` for
``SNDRV_PCM_HW_PARAM_PERIODS``::

  snd_pcm_hw_constraint_integer(substream->runtime,
                                SNDRV_PCM_HW_PARAM_PERIODS);

This assures that the number of periods is integer, hence the buffer
size is aligned with the period size.

The hw constraint is a very powerful mechanism to define the
preferred PCM configuration, and there are relevant helpers.
I won't give more details here, rather I would like to say, “Luke, use
the source.”

Control Interface
=================

General
-------

The control interface is used widely for many switches, sliders, etc.
which are accessed from user-space. Its most important use is the mixer
interface. In other words, since ALSA 0.9.x, all the mixer stuff is
implemented on the control kernel API.

ALSA has a well-defined AC97 control module. If your chip supports only
the AC97 and nothing else, you can skip this section.

The control API is defined in ``<sound/control.h>``. Include this file
if you want to add your own controls.

Definition of Controls
----------------------

To create a new control, you need to define the following three
callbacks: ``info``, ``get`` and ``put``. Then, define a
struct snd_kcontrol_new record, such as::


      static struct snd_kcontrol_new my_control = {
              .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
              .name = "PCM Playback Switch",
              .index = 0,
              .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
              .private_value = 0xffff,
              .info = my_control_info,
              .get = my_control_get,
              .put = my_control_put
      };


The ``iface`` field specifies the control type,
``SNDRV_CTL_ELEM_IFACE_XXX``, which is usually ``MIXER``. Use ``CARD``
for global controls that are not logically part of the mixer. If the
control is closely associated with some specific device on the sound
card, use ``HWDEP``, ``PCM``, ``RAWMIDI``, ``TIMER``, or ``SEQUENCER``,
and specify the device number with the ``device`` and ``subdevice``
fields.

The ``name`` is the name identifier string. Since ALSA 0.9.x, the
control name is very important, because its role is classified from
its name. There are pre-defined standard control names. The details
are described in the `Control Names`_ subsection.

The ``index`` field holds the index number of this control. If there
are several different controls with the same name, they can be
distinguished by the index number. This is the case when several
codecs exist on the card. If the index is zero, you can omit the
definition above. 

The ``access`` field contains the access type of this control. Give
the combination of bit masks, ``SNDRV_CTL_ELEM_ACCESS_XXX``,
there. The details will be explained in the `Access Flags`_
subsection.

The ``private_value`` field contains an arbitrary long integer value
for this record. When using the generic ``info``, ``get`` and ``put``
callbacks, you can pass a value through this field. If several small
numbers are necessary, you can combine them in bitwise. Or, it's
possible to store a pointer (casted to unsigned long) of some record in
this field, too. 

The ``tlv`` field can be used to provide metadata about the control;
see the `Metadata`_ subsection.

The other three are `Control Callbacks`_.

Control Names
-------------

There are some standards to define the control names. A control is
usually defined from the three parts as “SOURCE DIRECTION FUNCTION”.

The first, ``SOURCE``, specifies the source of the control, and is a
string such as “Master”, “PCM”, “CD” and “Line”. There are many
pre-defined sources.

The second, ``DIRECTION``, is one of the following strings according to
the direction of the control: “Playback”, “Capture”, “Bypass Playback”
and “Bypass Capture”. Or, it can be omitted, meaning both playback and
capture directions.

The third, ``FUNCTION``, is one of the following strings according to
the function of the control: “Switch”, “Volume” and “Route”.

The example of control names are, thus, “Master Capture Switch” or “PCM
Playback Volume”.

There are some exceptions:

Global capture and playback
~~~~~~~~~~~~~~~~~~~~~~~~~~~

“Capture Source”, “Capture Switch” and “Capture Volume” are used for the
global capture (input) source, switch and volume. Similarly, “Playback
Switch” and “Playback Volume” are used for the global output gain switch
and volume.

Tone-controls
~~~~~~~~~~~~~

tone-control switch and volumes are specified like “Tone Control - XXX”,
e.g. “Tone Control - Switch”, “Tone Control - Bass”, “Tone Control -
Center”.

3D controls
~~~~~~~~~~~

3D-control switches and volumes are specified like “3D Control - XXX”,
e.g. “3D Control - Switch”, “3D Control - Center”, “3D Control - Space”.

Mic boost
~~~~~~~~~

Mic-boost switch is set as “Mic Boost” or “Mic Boost (6dB)”.

More precise information can be found in
``Documentation/sound/designs/control-names.rst``.

Access Flags
------------

The access flag is the bitmask which specifies the access type of the
given control. The default access type is
``SNDRV_CTL_ELEM_ACCESS_READWRITE``, which means both read and write are
allowed to this control. When the access flag is omitted (i.e. = 0), it
is considered as ``READWRITE`` access by default.

When the control is read-only, pass ``SNDRV_CTL_ELEM_ACCESS_READ``
instead. In this case, you don't have to define the ``put`` callback.
Similarly, when the control is write-only (although it's a rare case),
you can use the ``WRITE`` flag instead, and you don't need the ``get``
callback.

If the control value changes frequently (e.g. the VU meter),
``VOLATILE`` flag should be given. This means that the control may be
changed without `Change notification`_. Applications should poll such
a control constantly.

When the control may be updated, but currently has no effect on anything,
setting the ``INACTIVE`` flag may be appropriate. For example, PCM
controls should be inactive while no PCM device is open.

There are ``LOCK`` and ``OWNER`` flags to change the write permissions.

Control Callbacks
-----------------

info callback
~~~~~~~~~~~~~

The ``info`` callback is used to get detailed information on this
control. This must store the values of the given
struct snd_ctl_elem_info object. For example,
for a boolean control with a single element::


      static int snd_myctl_mono_info(struct snd_kcontrol *kcontrol,
                              struct snd_ctl_elem_info *uinfo)
      {
              uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
              uinfo->count = 1;
              uinfo->value.integer.min = 0;
              uinfo->value.integer.max = 1;
              return 0;
      }



The ``type`` field specifies the type of the control. There are
``BOOLEAN``, ``INTEGER``, ``ENUMERATED``, ``BYTES``, ``IEC958`` and
``INTEGER64``. The ``count`` field specifies the number of elements in
this control. For example, a stereo volume would have count = 2. The
``value`` field is a union, and the values stored depend on the
type. The boolean and integer types are identical.

The enumerated type is a bit different from the others. You'll need to
set the string for the selectec item index::

  static int snd_myctl_enum_info(struct snd_kcontrol *kcontrol,
                          struct snd_ctl_elem_info *uinfo)
  {
          static char *texts[4] = {
                  "First", "Second", "Third", "Fourth"
          };
          uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
          uinfo->count = 1;
          uinfo->value.enumerated.items = 4;
          if (uinfo->value.enumerated.item > 3)
                  uinfo->value.enumerated.item = 3;
          strcpy(uinfo->value.enumerated.name,
                 texts[uinfo->value.enumerated.item]);
          return 0;
  }

The above callback can be simplified with a helper function,
:c:func:`snd_ctl_enum_info()`. The final code looks like below.
(You can pass ``ARRAY_SIZE(texts)`` instead of 4 in the third argument;
it's a matter of taste.)

::

  static int snd_myctl_enum_info(struct snd_kcontrol *kcontrol,
                          struct snd_ctl_elem_info *uinfo)
  {
          static char *texts[4] = {
                  "First", "Second", "Third", "Fourth"
          };
          return snd_ctl_enum_info(uinfo, 1, 4, texts);
  }


Some common info callbacks are available for your convenience:
:c:func:`snd_ctl_boolean_mono_info()` and
:c:func:`snd_ctl_boolean_stereo_info()`. Obviously, the former
is an info callback for a mono channel boolean item, just like
:c:func:`snd_myctl_mono_info()` above, and the latter is for a
stereo channel boolean item.

get callback
~~~~~~~~~~~~

This callback is used to read the current value of the control, so it
can be returned to user-space.

For example::

      static int snd_myctl_get(struct snd_kcontrol *kcontrol,
                               struct snd_ctl_elem_value *ucontrol)
      {
              struct mychip *chip = snd_kcontrol_chip(kcontrol);
              ucontrol->value.integer.value[0] = get_some_value(chip);
              return 0;
      }



The ``value`` field depends on the type of control as well as on the
info callback. For example, the sb driver uses this field to store the
register offset, the bit-shift and the bit-mask. The ``private_value``
field is set as follows::

  .private_value = reg | (shift << 16) | (mask << 24)

and is retrieved in callbacks like::

  static int snd_sbmixer_get_single(struct snd_kcontrol *kcontrol,
                                    struct snd_ctl_elem_value *ucontrol)
  {
          int reg = kcontrol->private_value & 0xff;
          int shift = (kcontrol->private_value >> 16) & 0xff;
          int mask = (kcontrol->private_value >> 24) & 0xff;
          ....
  }

In the ``get`` callback, you have to fill all the elements if the
control has more than one element, i.e. ``count > 1``. In the example
above, we filled only one element (``value.integer.value[0]``) since
``count = 1`` is assumed.

put callback
~~~~~~~~~~~~

This callback is used to write a value coming from user-space.

For example::

      static int snd_myctl_put(struct snd_kcontrol *kcontrol,
                               struct snd_ctl_elem_value *ucontrol)
      {
              struct mychip *chip = snd_kcontrol_chip(kcontrol);
              int changed = 0;
              if (chip->current_value !=
                   ucontrol->value.integer.value[0]) {
                      change_current_value(chip,
                                  ucontrol->value.integer.value[0]);
                      changed = 1;
              }
              return changed;
      }



As seen above, you have to return 1 if the value is changed. If the
value is not changed, return 0 instead. If any fatal error happens,
return a negative error code as usual.

As in the ``get`` callback, when the control has more than one
element, all elements must be evaluated in this callback, too.

Callbacks are not atomic
~~~~~~~~~~~~~~~~~~~~~~~~

All these three callbacks are not-atomic.

Control Constructor
-------------------

When everything is ready, finally we can create a new control. To create
a control, there are two functions to be called,
:c:func:`snd_ctl_new1()` and :c:func:`snd_ctl_add()`.

In the simplest way, you can do it like this::

  err = snd_ctl_add(card, snd_ctl_new1(&my_control, chip));
  if (err < 0)
          return err;

where ``my_control`` is the struct snd_kcontrol_new object defined above,
and chip is the object pointer to be passed to kcontrol->private_data which
can be referred to in callbacks.

:c:func:`snd_ctl_new1()` allocates a new struct snd_kcontrol instance, and
:c:func:`snd_ctl_add()` assigns the given control component to the
card.

Change Notification
-------------------

If you need to change and update a control in the interrupt routine, you
can call :c:func:`snd_ctl_notify()`. For example::

  snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE, id_pointer);

This function takes the card pointer, the event-mask, and the control id
pointer for the notification. The event-mask specifies the types of
notification, for example, in the above example, the change of control
values is notified. The id pointer is the pointer of struct snd_ctl_elem_id
to be notified. You can find some examples in ``es1938.c`` or ``es1968.c``
for hardware volume interrupts.

Metadata
--------

To provide information about the dB values of a mixer control, use one of
the ``DECLARE_TLV_xxx`` macros from ``<sound/tlv.h>`` to define a
variable containing this information, set the ``tlv.p`` field to point to
this variable, and include the ``SNDRV_CTL_ELEM_ACCESS_TLV_READ`` flag
in the ``access`` field; like this::

  static DECLARE_TLV_DB_SCALE(db_scale_my_control, -4050, 150, 0);

  static struct snd_kcontrol_new my_control = {
          ...
          .access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
                    SNDRV_CTL_ELEM_ACCESS_TLV_READ,
          ...
          .tlv.p = db_scale_my_control,
  };


The :c:func:`DECLARE_TLV_DB_SCALE()` macro defines information
about a mixer control where each step in the control's value changes the
dB value by a constant dB amount. The first parameter is the name of the
variable to be defined. The second parameter is the minimum value, in
units of 0.01 dB. The third parameter is the step size, in units of 0.01
dB. Set the fourth parameter to 1 if the minimum value actually mutes
the control.

The :c:func:`DECLARE_TLV_DB_LINEAR()` macro defines information
about a mixer control where the control's value affects the output
linearly. The first parameter is the name of the variable to be defined.
The second parameter is the minimum value, in units of 0.01 dB. The
third parameter is the maximum value, in units of 0.01 dB. If the
minimum value mutes the control, set the second parameter to
``TLV_DB_GAIN_MUTE``.

API for AC97 Codec
==================

General
-------

The ALSA AC97 codec layer is a well-defined one, and you don't have to
write much code to control it. Only low-level control routines are
necessary. The AC97 codec API is defined in ``<sound/ac97_codec.h>``.

Full Code Example
-----------------

::

      struct mychip {
              ....
              struct snd_ac97 *ac97;
              ....
      };

      static unsigned short snd_mychip_ac97_read(struct snd_ac97 *ac97,
                                                 unsigned short reg)
      {
              struct mychip *chip = ac97->private_data;
              ....
              /* read a register value here from the codec */
              return the_register_value;
      }

      static void snd_mychip_ac97_write(struct snd_ac97 *ac97,
                                       unsigned short reg, unsigned short val)
      {
              struct mychip *chip = ac97->private_data;
              ....
              /* write the given register value to the codec */
      }

      static int snd_mychip_ac97(struct mychip *chip)
      {
              struct snd_ac97_bus *bus;
              struct snd_ac97_template ac97;
              int err;
              static struct snd_ac97_bus_ops ops = {
                      .write = snd_mychip_ac97_write,
                      .read = snd_mychip_ac97_read,
              };

              err = snd_ac97_bus(chip->card, 0, &ops, NULL, &bus);
              if (err < 0)
                      return err;
              memset(&ac97, 0, sizeof(ac97));
              ac97.private_data = chip;
              return snd_ac97_mixer(bus, &ac97, &chip->ac97);
      }


AC97 Constructor
----------------

To create an ac97 instance, first call :c:func:`snd_ac97_bus()`
with an ``ac97_bus_ops_t`` record with callback functions::

  struct snd_ac97_bus *bus;
  static struct snd_ac97_bus_ops ops = {
        .write = snd_mychip_ac97_write,
        .read = snd_mychip_ac97_read,
  };

  snd_ac97_bus(card, 0, &ops, NULL, &pbus);

The bus record is shared among all belonging ac97 instances.

And then call :c:func:`snd_ac97_mixer()` with a struct snd_ac97_template
record together with the bus pointer created above::

  struct snd_ac97_template ac97;
  int err;

  memset(&ac97, 0, sizeof(ac97));
  ac97.private_data = chip;
  snd_ac97_mixer(bus, &ac97, &chip->ac97);

where chip->ac97 is a pointer to a newly created ``ac97_t``
instance. In this case, the chip pointer is set as the private data,
so that the read/write callback functions can refer to this chip
instance. This instance is not necessarily stored in the chip
record. If you need to change the register values from the driver, or
need the suspend/resume of ac97 codecs, keep this pointer to pass to
the corresponding functions.

AC97 Callbacks
--------------

The standard callbacks are ``read`` and ``write``. Obviously they
correspond to the functions for read and write accesses to the
hardware low-level codes.

The ``read`` callback returns the register value specified in the
argument::

  static unsigned short snd_mychip_ac97_read(struct snd_ac97 *ac97,
                                             unsigned short reg)
  {
          struct mychip *chip = ac97->private_data;
          ....
          return the_register_value;
  }

Here, the chip can be cast from ``ac97->private_data``.

Meanwhile, the ``write`` callback is used to set the register
value::

  static void snd_mychip_ac97_write(struct snd_ac97 *ac97,
                       unsigned short reg, unsigned short val)


These callbacks are non-atomic like the control API callbacks.

There are also other callbacks: ``reset``, ``wait`` and ``init``.

The ``reset`` callback is used to reset the codec. If the chip
requires a special kind of reset, you can define this callback.

The ``wait`` callback is used to add some waiting time in the standard
initialization of the codec. If the chip requires the extra waiting
time, define this callback.

The ``init`` callback is used for additional initialization of the
codec.

Updating Registers in The Driver
--------------------------------

If you need to access to the codec from the driver, you can call the
following functions: :c:func:`snd_ac97_write()`,
:c:func:`snd_ac97_read()`, :c:func:`snd_ac97_update()` and
:c:func:`snd_ac97_update_bits()`.

Both :c:func:`snd_ac97_write()` and
:c:func:`snd_ac97_update()` functions are used to set a value to
the given register (``AC97_XXX``). The difference between them is that
:c:func:`snd_ac97_update()` doesn't write a value if the given
value has been already set, while :c:func:`snd_ac97_write()`
always rewrites the value::

  snd_ac97_write(ac97, AC97_MASTER, 0x8080);
  snd_ac97_update(ac97, AC97_MASTER, 0x8080);

:c:func:`snd_ac97_read()` is used to read the value of the given
register. For example::

  value = snd_ac97_read(ac97, AC97_MASTER);

:c:func:`snd_ac97_update_bits()` is used to update some bits in
the given register::

  snd_ac97_update_bits(ac97, reg, mask, value);

Also, there is a function to change the sample rate (of a given register
such as ``AC97_PCM_FRONT_DAC_RATE``) when VRA or DRA is supported by the
codec: :c:func:`snd_ac97_set_rate()`::

  snd_ac97_set_rate(ac97, AC97_PCM_FRONT_DAC_RATE, 44100);


The following registers are available to set the rate:
``AC97_PCM_MIC_ADC_RATE``, ``AC97_PCM_FRONT_DAC_RATE``,
``AC97_PCM_LR_ADC_RATE``, ``AC97_SPDIF``. When ``AC97_SPDIF`` is
specified, the register is not really changed but the corresponding
IEC958 status bits will be updated.

Clock Adjustment
----------------

In some chips, the clock of the codec isn't 48000 but using a PCI clock
(to save a quartz!). In this case, change the field ``bus->clock`` to
the corresponding value. For example, intel8x0 and es1968 drivers have
their own function to read from the clock.

Proc Files
----------

The ALSA AC97 interface will create a proc file such as
``/proc/asound/card0/codec97#0/ac97#0-0`` and ``ac97#0-0+regs``. You
can refer to these files to see the current status and registers of
the codec.

Multiple Codecs
---------------

When there are several codecs on the same card, you need to call
:c:func:`snd_ac97_mixer()` multiple times with ``ac97.num=1`` or
greater. The ``num`` field specifies the codec number.

If you set up multiple codecs, you either need to write different
callbacks for each codec or check ``ac97->num`` in the callback
routines.

MIDI (MPU401-UART) Interface
============================

General
-------

Many soundcards have built-in MIDI (MPU401-UART) interfaces. When the
soundcard supports the standard MPU401-UART interface, most likely you
can use the ALSA MPU401-UART API. The MPU401-UART API is defined in
``<sound/mpu401.h>``.

Some soundchips have a similar but slightly different implementation of
mpu401 stuff. For example, emu10k1 has its own mpu401 routines.

MIDI Constructor
----------------

To create a rawmidi object, call :c:func:`snd_mpu401_uart_new()`::

  struct snd_rawmidi *rmidi;
  snd_mpu401_uart_new(card, 0, MPU401_HW_MPU401, port, info_flags,
                      irq, &rmidi);


The first argument is the card pointer, and the second is the index of
this component. You can create up to 8 rawmidi devices.

The third argument is the type of the hardware, ``MPU401_HW_XXX``. If
it's not a special one, you can use ``MPU401_HW_MPU401``.

The 4th argument is the I/O port address. Many backward-compatible
MPU401 have an I/O port such as 0x330. Or, it might be a part of its own
PCI I/O region. It depends on the chip design.

The 5th argument is a bitflag for additional information. When the I/O
port address above is part of the PCI I/O region, the MPU401 I/O port
might have been already allocated (reserved) by the driver itself. In
such a case, pass a bit flag ``MPU401_INFO_INTEGRATED``, and the
mpu401-uart layer will allocate the I/O ports by itself.

When the controller supports only the input or output MIDI stream, pass
the ``MPU401_INFO_INPUT`` or ``MPU401_INFO_OUTPUT`` bitflag,
respectively. Then the rawmidi instance is created as a single stream.

``MPU401_INFO_MMIO`` bitflag is used to change the access method to MMIO
(via readb and writeb) instead of iob and outb. In this case, you have
to pass the iomapped address to :c:func:`snd_mpu401_uart_new()`.

When ``MPU401_INFO_TX_IRQ`` is set, the output stream isn't checked in
the default interrupt handler. The driver needs to call
:c:func:`snd_mpu401_uart_interrupt_tx()` by itself to start
processing the output stream in the irq handler.

If the MPU-401 interface shares its interrupt with the other logical
devices on the card, set ``MPU401_INFO_IRQ_HOOK`` (see
`below <MIDI Interrupt Handler_>`__).

Usually, the port address corresponds to the command port and port + 1
corresponds to the data port. If not, you may change the ``cport``
field of struct snd_mpu401 manually afterward.
However, struct snd_mpu401 pointer is
not returned explicitly by :c:func:`snd_mpu401_uart_new()`. You
need to cast ``rmidi->private_data`` to struct snd_mpu401 explicitly::

  struct snd_mpu401 *mpu;
  mpu = rmidi->private_data;

and reset the ``cport`` as you like::

  mpu->cport = my_own_control_port;

The 6th argument specifies the ISA irq number that will be allocated. If
no interrupt is to be allocated (because your code is already allocating
a shared interrupt, or because the device does not use interrupts), pass
-1 instead. For a MPU-401 device without an interrupt, a polling timer
will be used instead.

MIDI Interrupt Handler
----------------------

When the interrupt is allocated in
:c:func:`snd_mpu401_uart_new()`, an exclusive ISA interrupt
handler is automatically used, hence you don't have anything else to do
than creating the mpu401 stuff. Otherwise, you have to set
``MPU401_INFO_IRQ_HOOK``, and call
:c:func:`snd_mpu401_uart_interrupt()` explicitly from your own
interrupt handler when it has determined that a UART interrupt has
occurred.

In this case, you need to pass the private_data of the returned rawmidi
object from :c:func:`snd_mpu401_uart_new()` as the second
argument of :c:func:`snd_mpu401_uart_interrupt()`::

  snd_mpu401_uart_interrupt(irq, rmidi->private_data, regs);


RawMIDI Interface
=================

Overview
--------

The raw MIDI interface is used for hardware MIDI ports that can be
accessed as a byte stream. It is not used for synthesizer chips that do
not directly understand MIDI.

ALSA handles file and buffer management. All you have to do is to write
some code to move data between the buffer and the hardware.

The rawmidi API is defined in ``<sound/rawmidi.h>``.

RawMIDI Constructor
-------------------

To create a rawmidi device, call the :c:func:`snd_rawmidi_new()`
function::

  struct snd_rawmidi *rmidi;
  err = snd_rawmidi_new(chip->card, "MyMIDI", 0, outs, ins, &rmidi);
  if (err < 0)
          return err;
  rmidi->private_data = chip;
  strcpy(rmidi->name, "My MIDI");
  rmidi->info_flags = SNDRV_RAWMIDI_INFO_OUTPUT |
                      SNDRV_RAWMIDI_INFO_INPUT |
                      SNDRV_RAWMIDI_INFO_DUPLEX;

The first argument is the card pointer, the second argument is the ID
string.

The third argument is the index of this component. You can create up to
8 rawmidi devices.

The fourth and fifth arguments are the number of output and input
substreams, respectively, of this device (a substream is the equivalent
of a MIDI port).

Set the ``info_flags`` field to specify the capabilities of the
device. Set ``SNDRV_RAWMIDI_INFO_OUTPUT`` if there is at least one
output port, ``SNDRV_RAWMIDI_INFO_INPUT`` if there is at least one
input port, and ``SNDRV_RAWMIDI_INFO_DUPLEX`` if the device can handle
output and input at the same time.

After the rawmidi device is created, you need to set the operators
(callbacks) for each substream. There are helper functions to set the
operators for all the substreams of a device::

  snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_mymidi_output_ops);
  snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_mymidi_input_ops);

The operators are usually defined like this::

  static struct snd_rawmidi_ops snd_mymidi_output_ops = {
          .open =    snd_mymidi_output_open,
          .close =   snd_mymidi_output_close,
          .trigger = snd_mymidi_output_trigger,
  };

These callbacks are explained in the `RawMIDI Callbacks`_ section.

If there are more than one substream, you should give a unique name to
each of them::

  struct snd_rawmidi_substream *substream;
  list_for_each_entry(substream,
                      &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams,
                      list {
          sprintf(substream->name, "My MIDI Port %d", substream->number + 1);
  }
  /* same for SNDRV_RAWMIDI_STREAM_INPUT */

RawMIDI Callbacks
-----------------

In all the callbacks, the private data that you've set for the rawmidi
device can be accessed as ``substream->rmidi->private_data``.

If there is more than one port, your callbacks can determine the port
index from the struct snd_rawmidi_substream data passed to each
callback::

  struct snd_rawmidi_substream *substream;
  int index = substream->number;

RawMIDI open callback
~~~~~~~~~~~~~~~~~~~~~

::

      static int snd_xxx_open(struct snd_rawmidi_substream *substream);


This is called when a substream is opened. You can initialize the
hardware here, but you shouldn't start transmitting/receiving data yet.

RawMIDI close callback
~~~~~~~~~~~~~~~~~~~~~~

::

      static int snd_xxx_close(struct snd_rawmidi_substream *substream);

Guess what.

The ``open`` and ``close`` callbacks of a rawmidi device are
serialized with a mutex, and can sleep.

Rawmidi trigger callback for output substreams
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

      static void snd_xxx_output_trigger(struct snd_rawmidi_substream *substream, int up);


This is called with a nonzero ``up`` parameter when there is some data
in the substream buffer that must be transmitted.

To read data from the buffer, call
:c:func:`snd_rawmidi_transmit_peek()`. It will return the number
of bytes that have been read; this will be less than the number of bytes
requested when there are no more data in the buffer. After the data have
been transmitted successfully, call
:c:func:`snd_rawmidi_transmit_ack()` to remove the data from the
substream buffer::

  unsigned char data;
  while (snd_rawmidi_transmit_peek(substream, &data, 1) == 1) {
          if (snd_mychip_try_to_transmit(data))
                  snd_rawmidi_transmit_ack(substream, 1);
          else
                  break; /* hardware FIFO full */
  }

If you know beforehand that the hardware will accept data, you can use
the :c:func:`snd_rawmidi_transmit()` function which reads some
data and removes them from the buffer at once::

  while (snd_mychip_transmit_possible()) {
          unsigned char data;
          if (snd_rawmidi_transmit(substream, &data, 1) != 1)
                  break; /* no more data */
          snd_mychip_transmit(data);
  }

If you know beforehand how many bytes you can accept, you can use a
buffer size greater than one with the ``snd_rawmidi_transmit*()`` functions.

The ``trigger`` callback must not sleep. If the hardware FIFO is full
before the substream buffer has been emptied, you have to continue
transmitting data later, either in an interrupt handler, or with a
timer if the hardware doesn't have a MIDI transmit interrupt.

The ``trigger`` callback is called with a zero ``up`` parameter when
the transmission of data should be aborted.

RawMIDI trigger callback for input substreams
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

      static void snd_xxx_input_trigger(struct snd_rawmidi_substream *substream, int up);


This is called with a nonzero ``up`` parameter to enable receiving data,
or with a zero ``up`` parameter do disable receiving data.

The ``trigger`` callback must not sleep; the actual reading of data
from the device is usually done in an interrupt handler.

When data reception is enabled, your interrupt handler should call
:c:func:`snd_rawmidi_receive()` for all received data::

  void snd_mychip_midi_interrupt(...)
  {
          while (mychip_midi_available()) {
                  unsigned char data;
                  data = mychip_midi_read();
                  snd_rawmidi_receive(substream, &data, 1);
          }
  }


drain callback
~~~~~~~~~~~~~~

::

      static void snd_xxx_drain(struct snd_rawmidi_substream *substream);


This is only used with output substreams. This function should wait
until all data read from the substream buffer have been transmitted.
This ensures that the device can be closed and the driver unloaded
without losing data.

This callback is optional. If you do not set ``drain`` in the struct
snd_rawmidi_ops structure, ALSA will simply wait for 50 milliseconds
instead.

Miscellaneous Devices
=====================

FM OPL3
-------

The FM OPL3 is still used in many chips (mainly for backward
compatibility). ALSA has a nice OPL3 FM control layer, too. The OPL3 API
is defined in ``<sound/opl3.h>``.

FM registers can be directly accessed through the direct-FM API, defined
in ``<sound/asound_fm.h>``. In ALSA native mode, FM registers are
accessed through the Hardware-Dependent Device direct-FM extension API,
whereas in OSS compatible mode, FM registers can be accessed with the
OSS direct-FM compatible API in ``/dev/dmfmX`` device.

To create the OPL3 component, you have two functions to call. The first
one is a constructor for the ``opl3_t`` instance::

  struct snd_opl3 *opl3;
  snd_opl3_create(card, lport, rport, OPL3_HW_OPL3_XXX,
                  integrated, &opl3);

The first argument is the card pointer, the second one is the left port
address, and the third is the right port address. In most cases, the
right port is placed at the left port + 2.

The fourth argument is the hardware type.

When the left and right ports have been already allocated by the card
driver, pass non-zero to the fifth argument (``integrated``). Otherwise,
the opl3 module will allocate the specified ports by itself.

When the accessing the hardware requires special method instead of the
standard I/O access, you can create opl3 instance separately with
:c:func:`snd_opl3_new()`::

  struct snd_opl3 *opl3;
  snd_opl3_new(card, OPL3_HW_OPL3_XXX, &opl3);

Then set ``command``, ``private_data`` and ``private_free`` for the
private access function, the private data and the destructor. The
``l_port`` and ``r_port`` are not necessarily set. Only the command
must be set properly. You can retrieve the data from the
``opl3->private_data`` field. 

After creating the opl3 instance via :c:func:`snd_opl3_new()`,
call :c:func:`snd_opl3_init()` to initialize the chip to the
proper state. Note that :c:func:`snd_opl3_create()` always calls
it internally.

If the opl3 instance is created successfully, then create a hwdep device
for this opl3::

  struct snd_hwdep *opl3hwdep;
  snd_opl3_hwdep_new(opl3, 0, 1, &opl3hwdep);

The first argument is the ``opl3_t`` instance you created, and the
second is the index number, usually 0.

The third argument is the index-offset for the sequencer client assigned
to the OPL3 port. When there is an MPU401-UART, give 1 for here (UART
always takes 0).

Hardware-Dependent Devices
--------------------------

Some chips need user-space access for special controls or for loading
the micro code. In such a case, you can create a hwdep
(hardware-dependent) device. The hwdep API is defined in
``<sound/hwdep.h>``. You can find examples in opl3 driver or
``isa/sb/sb16_csp.c``.

The creation of the ``hwdep`` instance is done via
:c:func:`snd_hwdep_new()`::

  struct snd_hwdep *hw;
  snd_hwdep_new(card, "My HWDEP", 0, &hw);

where the third argument is the index number.

You can then pass any pointer value to the ``private_data``. If you
assign private data, you should define a destructor, too. The
destructor function is set in the ``private_free`` field::

  struct mydata *p = kmalloc(sizeof(*p), GFP_KERNEL);
  hw->private_data = p;
  hw->private_free = mydata_free;

and the implementation of the destructor would be::

  static void mydata_free(struct snd_hwdep *hw)
  {
          struct mydata *p = hw->private_data;
          kfree(p);
  }

The arbitrary file operations can be defined for this instance. The file
operators are defined in the ``ops`` table. For example, assume that
this chip needs an ioctl::

  hw->ops.open = mydata_open;
  hw->ops.ioctl = mydata_ioctl;
  hw->ops.release = mydata_release;

And implement the callback functions as you like.

IEC958 (S/PDIF)
---------------

Usually the controls for IEC958 devices are implemented via the control
interface. There is a macro to compose a name string for IEC958
controls, :c:func:`SNDRV_CTL_NAME_IEC958()` defined in
``<include/asound.h>``.

There are some standard controls for IEC958 status bits. These controls
use the type ``SNDRV_CTL_ELEM_TYPE_IEC958``, and the size of element is
fixed as 4 bytes array (value.iec958.status[x]). For the ``info``
callback, you don't specify the value field for this type (the count
field must be set, though).

“IEC958 Playback Con Mask” is used to return the bit-mask for the IEC958
status bits of consumer mode. Similarly, “IEC958 Playback Pro Mask”
returns the bitmask for professional mode. They are read-only controls.

Meanwhile, “IEC958 Playback Default” control is defined for getting and
setting the current default IEC958 bits.

Due to historical reasons, both variants of the Playback Mask and the
Playback Default controls can be implemented on either a
``SNDRV_CTL_ELEM_IFACE_PCM`` or a ``SNDRV_CTL_ELEM_IFACE_MIXER`` iface.
Drivers should expose the mask and default on the same iface though.

In addition, you can define the control switches to enable/disable or to
set the raw bit mode. The implementation will depend on the chip, but
the control should be named as “IEC958 xxx”, preferably using the
:c:func:`SNDRV_CTL_NAME_IEC958()` macro.

You can find several cases, for example, ``pci/emu10k1``,
``pci/ice1712``, or ``pci/cmipci.c``.

Buffer and Memory Management
============================

Buffer Types
------------

ALSA provides several different buffer allocation functions depending on
the bus and the architecture. All these have a consistent API. The
allocation of physically-contiguous pages is done via the
:c:func:`snd_malloc_xxx_pages()` function, where xxx is the bus
type.

The allocation of pages with fallback is done via
:c:func:`snd_dma_alloc_pages_fallback()`. This function tries
to allocate the specified number of pages, but if not enough pages are
available, it tries to reduce the request size until enough space
is found, down to one page.

To release the pages, call the :c:func:`snd_dma_free_pages()`
function.

Usually, ALSA drivers try to allocate and reserve a large contiguous
physical space at the time the module is loaded for later use. This
is called “pre-allocation”. As already written, you can call the
following function at PCM instance construction time (in the case of PCI
bus)::

  snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
                                        &pci->dev, size, max);

where ``size`` is the byte size to be pre-allocated and ``max`` is
the maximum size settable via the ``prealloc`` proc file. The
allocator will try to get an area as large as possible within the
given size.

The second argument (type) and the third argument (device pointer) are
dependent on the bus. For normal devices, pass the device pointer
(typically identical as ``card->dev``) to the third argument with
``SNDRV_DMA_TYPE_DEV`` type.

A continuous buffer unrelated to the
bus can be pre-allocated with ``SNDRV_DMA_TYPE_CONTINUOUS`` type.
You can pass NULL to the device pointer in that case, which is the
default mode implying to allocate with the ``GFP_KERNEL`` flag.
If you need a restricted (lower) address, set up the coherent DMA mask
bits for the device, and pass the device pointer, like the normal
device memory allocations.  For this type, it's still allowed to pass
NULL to the device pointer, too, if no address restriction is needed.

For the scatter-gather buffers, use ``SNDRV_DMA_TYPE_DEV_SG`` with the
device pointer (see the `Non-Contiguous Buffers`_ section).

Once the buffer is pre-allocated, you can use the allocator in the
``hw_params`` callback::

  snd_pcm_lib_malloc_pages(substream, size);

Note that you have to pre-allocate to use this function.

But most drivers use the "managed buffer allocation mode" instead
of manual allocation and release.
This is done by calling :c:func:`snd_pcm_set_managed_buffer_all()`
instead of :c:func:`snd_pcm_lib_preallocate_pages_for_all()`::

  snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV,
                                 &pci->dev, size, max);

where the passed arguments are identical for both functions.
The difference in the managed mode is that PCM core will call
:c:func:`snd_pcm_lib_malloc_pages()` internally already before calling
the PCM ``hw_params`` callback, and call :c:func:`snd_pcm_lib_free_pages()`
after the PCM ``hw_free`` callback automatically.  So the driver
doesn't have to call these functions explicitly in its callback any
longer.  This allows many drivers to have NULL ``hw_params`` and
``hw_free`` entries.

External Hardware Buffers
-------------------------

Some chips have their own hardware buffers and DMA transfer from the
host memory is not available. In such a case, you need to either 1)
copy/set the audio data directly to the external hardware buffer, or 2)
make an intermediate buffer and copy/set the data from it to the
external hardware buffer in interrupts (or in tasklets, preferably).

The first case works fine if the external hardware buffer is large
enough. This method doesn't need any extra buffers and thus is more
efficient. You need to define the ``copy_user`` and ``copy_kernel``
callbacks for the data transfer, in addition to the ``fill_silence``
callback for playback. However, there is a drawback: it cannot be
mmapped. The examples are GUS's GF1 PCM or emu8000's wavetable PCM.

The second case allows for mmap on the buffer, although you have to
handle an interrupt or a tasklet to transfer the data from the
intermediate buffer to the hardware buffer. You can find an example in
the vxpocket driver.

Another case is when the chip uses a PCI memory-map region for the
buffer instead of the host memory. In this case, mmap is available only
on certain architectures like the Intel one. In non-mmap mode, the data
cannot be transferred as in the normal way. Thus you need to define the
``copy_user``, ``copy_kernel`` and ``fill_silence`` callbacks as well,
as in the cases above. Examples are found in ``rme32.c`` and
``rme96.c``.

The implementation of the ``copy_user``, ``copy_kernel`` and
``silence`` callbacks depends upon whether the hardware supports
interleaved or non-interleaved samples. The ``copy_user`` callback is
defined like below, a bit differently depending on whether the direction
is playback or capture::

  static int playback_copy_user(struct snd_pcm_substream *substream,
               int channel, unsigned long pos,
               void __user *src, unsigned long count);
  static int capture_copy_user(struct snd_pcm_substream *substream,
               int channel, unsigned long pos,
               void __user *dst, unsigned long count);

In the case of interleaved samples, the second argument (``channel``) is
not used. The third argument (``pos``) specifies the position in bytes.

The meaning of the fourth argument is different between playback and
capture. For playback, it holds the source data pointer, and for
capture, it's the destination data pointer.

The last argument is the number of bytes to be copied.

What you have to do in this callback is again different between playback
and capture directions. In the playback case, you copy the given amount
of data (``count``) at the specified pointer (``src``) to the specified
offset (``pos``) in the hardware buffer. When coded like memcpy-like
way, the copy would look like::

  my_memcpy_from_user(my_buffer + pos, src, count);

For the capture direction, you copy the given amount of data (``count``)
at the specified offset (``pos``) in the hardware buffer to the
specified pointer (``dst``)::

  my_memcpy_to_user(dst, my_buffer + pos, count);

Here the functions are named ``from_user`` and ``to_user`` because
it's the user-space buffer that is passed to these callbacks.  That
is, the callback is supposed to copy data from/to the user-space
directly to/from the hardware buffer.

Careful readers might notice that these callbacks receive the
arguments in bytes, not in frames like other callbacks.  It's because
this makes coding easier like in the examples above, and also it makes
it easier to unify both the interleaved and non-interleaved cases, as
explained below.

In the case of non-interleaved samples, the implementation will be a bit
more complicated.  The callback is called for each channel, passed in
the second argument, so in total it's called N times per transfer.

The meaning of the other arguments are almost the same as in the
interleaved case.  The callback is supposed to copy the data from/to
the given user-space buffer, but only for the given channel. For
details, please check ``isa/gus/gus_pcm.c`` or ``pci/rme9652/rme9652.c``
as examples.

The above callbacks are the copies from/to the user-space buffer.  There
are some cases where we want to copy from/to the kernel-space buffer
instead.  In such a case, the ``copy_kernel`` callback is called.  It'd
look like::

  static int playback_copy_kernel(struct snd_pcm_substream *substream,
               int channel, unsigned long pos,
               void *src, unsigned long count);
  static int capture_copy_kernel(struct snd_pcm_substream *substream,
               int channel, unsigned long pos,
               void *dst, unsigned long count);

As found easily, the only difference is that the buffer pointer is
without a ``__user`` prefix; that is, a kernel-buffer pointer is passed
in the fourth argument.  Correspondingly, the implementation would be
a version without the user-copy, such as::

  my_memcpy(my_buffer + pos, src, count);

Usually for the playback, another callback ``fill_silence`` is
defined.  It's implemented in a similar way as the copy callbacks
above::

  static int silence(struct snd_pcm_substream *substream, int channel,
                     unsigned long pos, unsigned long count);

The meanings of arguments are the same as in the ``copy_user`` and
``copy_kernel`` callbacks, although there is no buffer pointer
argument. In the case of interleaved samples, the channel argument has
no meaning, as for the ``copy_*`` callbacks.

The role of the ``fill_silence`` callback is to set the given amount
(``count``) of silence data at the specified offset (``pos``) in the
hardware buffer. Suppose that the data format is signed (that is, the
silent-data is 0), and the implementation using a memset-like function
would look like::

  my_memset(my_buffer + pos, 0, count);

In the case of non-interleaved samples, again, the implementation
becomes a bit more complicated, as it's called N times per transfer
for each channel. See, for example, ``isa/gus/gus_pcm.c``.

Non-Contiguous Buffers
----------------------

If your hardware supports a page table as in emu10k1 or buffer
descriptors as in via82xx, you can use scatter-gather (SG) DMA. ALSA
provides an interface for handling SG-buffers. The API is provided in
``<sound/pcm.h>``.

For creating the SG-buffer handler, call
:c:func:`snd_pcm_set_managed_buffer()` or
:c:func:`snd_pcm_set_managed_buffer_all()` with
``SNDRV_DMA_TYPE_DEV_SG`` in the PCM constructor like for other PCI
pre-allocations. You need to pass ``&pci->dev``, where pci is
the struct pci_dev pointer of the chip as well::

  snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
                                 &pci->dev, size, max);

The ``struct snd_sg_buf`` instance is created as
``substream->dma_private`` in turn. You can cast the pointer like::

  struct snd_sg_buf *sgbuf = (struct snd_sg_buf *)substream->dma_private;

Then in the :c:func:`snd_pcm_lib_malloc_pages()` call, the common SG-buffer
handler will allocate the non-contiguous kernel pages of the given size
and map them as virtually contiguous memory. The virtual pointer
is addressed via runtime->dma_area. The physical address
(``runtime->dma_addr``) is set to zero, because the buffer is
physically non-contiguous. The physical address table is set up in
``sgbuf->table``. You can get the physical address at a certain offset
via :c:func:`snd_pcm_sgbuf_get_addr()`.

If you need to release the SG-buffer data explicitly, call the
standard API function :c:func:`snd_pcm_lib_free_pages()` as usual.

Vmalloc'ed Buffers
------------------

It's possible to use a buffer allocated via :c:func:`vmalloc()`, for
example, for an intermediate buffer.
You can simply allocate it via the standard
:c:func:`snd_pcm_lib_malloc_pages()` and co. after setting up the
buffer preallocation with ``SNDRV_DMA_TYPE_VMALLOC`` type::

  snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC,
                                 NULL, 0, 0);

NULL is passed as the device pointer argument, which indicates
that default pages (GFP_KERNEL and GFP_HIGHMEM) will be
allocated.

Also, note that zero is passed as both the size and the max size
argument here.  Since each vmalloc call should succeed at any time,
we don't need to pre-allocate the buffers like other continuous
pages.

Proc Interface
==============

ALSA provides an easy interface for procfs. The proc files are very
useful for debugging. I recommend you set up proc files if you write a
driver and want to get a running status or register dumps. The API is
found in ``<sound/info.h>``.

To create a proc file, call :c:func:`snd_card_proc_new()`::

  struct snd_info_entry *entry;
  int err = snd_card_proc_new(card, "my-file", &entry);

where the second argument specifies the name of the proc file to be
created. The above example will create a file ``my-file`` under the
card directory, e.g. ``/proc/asound/card0/my-file``.

Like other components, the proc entry created via
:c:func:`snd_card_proc_new()` will be registered and released
automatically in the card registration and release functions.

When the creation is successful, the function stores a new instance in
the pointer given in the third argument. It is initialized as a text
proc file for read only. To use this proc file as a read-only text file
as-is, set the read callback with private data via
:c:func:`snd_info_set_text_ops()`::

  snd_info_set_text_ops(entry, chip, my_proc_read);

where the second argument (``chip``) is the private data to be used in
the callback. The third parameter specifies the read buffer size and
the fourth (``my_proc_read``) is the callback function, which is
defined like::

  static void my_proc_read(struct snd_info_entry *entry,
                           struct snd_info_buffer *buffer);

In the read callback, use :c:func:`snd_iprintf()` for output
strings, which works just like normal :c:func:`printf()`. For
example::

  static void my_proc_read(struct snd_info_entry *entry,
                           struct snd_info_buffer *buffer)
  {
          struct my_chip *chip = entry->private_data;

          snd_iprintf(buffer, "This is my chip!\n");
          snd_iprintf(buffer, "Port = %ld\n", chip->port);
  }

The file permissions can be changed afterwards. By default, they are
read only for all users. If you want to add write permission for the
user (root by default), do as follows::

 entry->mode = S_IFREG | S_IRUGO | S_IWUSR;

and set the write buffer size and the callback::

  entry->c.text.write = my_proc_write;

In the write callback, you can use :c:func:`snd_info_get_line()`
to get a text line, and :c:func:`snd_info_get_str()` to retrieve
a string from the line. Some examples are found in
``core/oss/mixer_oss.c``, core/oss/and ``pcm_oss.c``.

For a raw-data proc-file, set the attributes as follows::

  static const struct snd_info_entry_ops my_file_io_ops = {
          .read = my_file_io_read,
  };

  entry->content = SNDRV_INFO_CONTENT_DATA;
  entry->private_data = chip;
  entry->c.ops = &my_file_io_ops;
  entry->size = 4096;
  entry->mode = S_IFREG | S_IRUGO;

For raw data, ``size`` field must be set properly. This specifies
the maximum size of the proc file access.

The read/write callbacks of raw mode are more direct than the text mode.
You need to use a low-level I/O functions such as
:c:func:`copy_from_user()` and :c:func:`copy_to_user()` to transfer the
data::

  static ssize_t my_file_io_read(struct snd_info_entry *entry,
                              void *file_private_data,
                              struct file *file,
                              char *buf,
                              size_t count,
                              loff_t pos)
  {
          if (copy_to_user(buf, local_data + pos, count))
                  return -EFAULT;
          return count;
  }

If the size of the info entry has been set up properly, ``count`` and
``pos`` are guaranteed to fit within 0 and the given size. You don't
have to check the range in the callbacks unless any other condition is
required.

Power Management
================

If the chip is supposed to work with suspend/resume functions, you need
to add power-management code to the driver. The additional code for
power-management should be ifdef-ed with ``CONFIG_PM``, or annotated
with __maybe_unused attribute; otherwise the compiler will complain.

If the driver *fully* supports suspend/resume that is, the device can be
properly resumed to its state when suspend was called, you can set the
``SNDRV_PCM_INFO_RESUME`` flag in the PCM info field. Usually, this is
possible when the registers of the chip can be safely saved and restored
to RAM. If this is set, the trigger callback is called with
``SNDRV_PCM_TRIGGER_RESUME`` after the resume callback completes.

Even if the driver doesn't support PM fully but partial suspend/resume
is still possible, it's still worthy to implement suspend/resume
callbacks. In such a case, applications would reset the status by
calling :c:func:`snd_pcm_prepare()` and restart the stream
appropriately. Hence, you can define suspend/resume callbacks below but
don't set the ``SNDRV_PCM_INFO_RESUME`` info flag to the PCM.

Note that the trigger with SUSPEND can always be called when
:c:func:`snd_pcm_suspend_all()` is called, regardless of the
``SNDRV_PCM_INFO_RESUME`` flag. The ``RESUME`` flag affects only the
behavior of :c:func:`snd_pcm_resume()`. (Thus, in theory,
``SNDRV_PCM_TRIGGER_RESUME`` isn't needed to be handled in the trigger
callback when no ``SNDRV_PCM_INFO_RESUME`` flag is set. But, it's better
to keep it for compatibility reasons.)

The driver needs to define the
suspend/resume hooks according to the bus the device is connected to. In
the case of PCI drivers, the callbacks look like below::

  static int __maybe_unused snd_my_suspend(struct device *dev)
  {
          .... /* do things for suspend */
          return 0;
  }
  static int __maybe_unused snd_my_resume(struct device *dev)
  {
          .... /* do things for suspend */
          return 0;
  }

The scheme of the real suspend job is as follows:

1. Retrieve the card and the chip data.

2. Call :c:func:`snd_power_change_state()` with
   ``SNDRV_CTL_POWER_D3hot`` to change the power status.

3. If AC97 codecs are used, call :c:func:`snd_ac97_suspend()` for
   each codec.

4. Save the register values if necessary.

5. Stop the hardware if necessary.

Typical code would look like::

  static int __maybe_unused mychip_suspend(struct device *dev)
  {
          /* (1) */
          struct snd_card *card = dev_get_drvdata(dev);
          struct mychip *chip = card->private_data;
          /* (2) */
          snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
          /* (3) */
          snd_ac97_suspend(chip->ac97);
          /* (4) */
          snd_mychip_save_registers(chip);
          /* (5) */
          snd_mychip_stop_hardware(chip);
          return 0;
  }


The scheme of the real resume job is as follows:

1. Retrieve the card and the chip data.

2. Re-initialize the chip.

3. Restore the saved registers if necessary.

4. Resume the mixer, e.g. by calling :c:func:`snd_ac97_resume()`.

5. Restart the hardware (if any).

6. Call :c:func:`snd_power_change_state()` with
   ``SNDRV_CTL_POWER_D0`` to notify the processes.

Typical code would look like::

  static int __maybe_unused mychip_resume(struct pci_dev *pci)
  {
          /* (1) */
          struct snd_card *card = dev_get_drvdata(dev);
          struct mychip *chip = card->private_data;
          /* (2) */
          snd_mychip_reinit_chip(chip);
          /* (3) */
          snd_mychip_restore_registers(chip);
          /* (4) */
          snd_ac97_resume(chip->ac97);
          /* (5) */
          snd_mychip_restart_chip(chip);
          /* (6) */
          snd_power_change_state(card, SNDRV_CTL_POWER_D0);
          return 0;
  }

Note that, at the time this callback gets called, the PCM stream has
been already suspended via its own PM ops calling
:c:func:`snd_pcm_suspend_all()` internally.

OK, we have all callbacks now. Let's set them up. In the initialization
of the card, make sure that you can get the chip data from the card
instance, typically via ``private_data`` field, in case you created the
chip data individually::

  static int snd_mychip_probe(struct pci_dev *pci,
                              const struct pci_device_id *pci_id)
  {
          ....
          struct snd_card *card;
          struct mychip *chip;
          int err;
          ....
          err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
                             0, &card);
          ....
          chip = kzalloc(sizeof(*chip), GFP_KERNEL);
          ....
          card->private_data = chip;
          ....
  }

When you created the chip data with :c:func:`snd_card_new()`, it's
anyway accessible via ``private_data`` field::

  static int snd_mychip_probe(struct pci_dev *pci,
                              const struct pci_device_id *pci_id)
  {
          ....
          struct snd_card *card;
          struct mychip *chip;
          int err;
          ....
          err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
                             sizeof(struct mychip), &card);
          ....
          chip = card->private_data;
          ....
  }

If you need space to save the registers, allocate the buffer for it
here, too, since it would be fatal if you cannot allocate a memory in
the suspend phase. The allocated buffer should be released in the
corresponding destructor.

And next, set suspend/resume callbacks to the pci_driver::

  static SIMPLE_DEV_PM_OPS(snd_my_pm_ops, mychip_suspend, mychip_resume);

  static struct pci_driver driver = {
          .name = KBUILD_MODNAME,
          .id_table = snd_my_ids,
          .probe = snd_my_probe,
          .remove = snd_my_remove,
          .driver.pm = &snd_my_pm_ops,
  };

Module Parameters
=================

There are standard module options for ALSA. At least, each module should
have the ``index``, ``id`` and ``enable`` options.

If the module supports multiple cards (usually up to 8 = ``SNDRV_CARDS``
cards), they should be arrays. The default initial values are defined
already as constants for easier programming::

  static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
  static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
  static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

If the module supports only a single card, they could be single
variables, instead. ``enable`` option is not always necessary in this
case, but it would be better to have a dummy option for compatibility.

The module parameters must be declared with the standard
``module_param()``, ``module_param_array()`` and
:c:func:`MODULE_PARM_DESC()` macros.

Typical code would look as below::

  #define CARD_NAME "My Chip"

  module_param_array(index, int, NULL, 0444);
  MODULE_PARM_DESC(index, "Index value for " CARD_NAME " soundcard.");
  module_param_array(id, charp, NULL, 0444);
  MODULE_PARM_DESC(id, "ID string for " CARD_NAME " soundcard.");
  module_param_array(enable, bool, NULL, 0444);
  MODULE_PARM_DESC(enable, "Enable " CARD_NAME " soundcard.");

Also, don't forget to define the module description and the license.
Especially, the recent modprobe requires to define the
module license as GPL, etc., otherwise the system is shown as “tainted”::

  MODULE_DESCRIPTION("Sound driver for My Chip");
  MODULE_LICENSE("GPL");


Device-Managed Resources
========================

In the examples above, all resources are allocated and released
manually.  But human beings are lazy in nature, especially developers
are lazier.  So there are some ways to automate the release part; it's
the (device-)managed resources aka devres or devm family.  For
example, an object allocated via :c:func:`devm_kmalloc()` will be
freed automatically at unbinding the device.

ALSA core provides also the device-managed helper, namely,
:c:func:`snd_devm_card_new()` for creating a card object.
Call this functions instead of the normal :c:func:`snd_card_new()`,
and you can forget the explicit :c:func:`snd_card_free()` call, as
it's called automagically at error and removal paths.

One caveat is that the call of :c:func:`snd_card_free()` would be put
at the beginning of the call chain only after you call
:c:func:`snd_card_register()`.

Also, the ``private_free`` callback is always called at the card free,
so be careful to put the hardware clean-up procedure in
``private_free`` callback.  It might be called even before you
actually set up at an earlier error path.  For avoiding such an
invalid initialization, you can set ``private_free`` callback after
:c:func:`snd_card_register()` call succeeds.

Another thing to be remarked is that you should use device-managed
helpers for each component as much as possible once when you manage
the card in that way.  Mixing up with the normal and the managed
resources may screw up the release order.


How To Put Your Driver Into ALSA Tree
=====================================

General
-------

So far, you've learned how to write the driver codes. And you might have
a question now: how to put my own driver into the ALSA driver tree? Here
(finally :) the standard procedure is described briefly.

Suppose that you create a new PCI driver for the card “xyz”. The card
module name would be snd-xyz. The new driver is usually put into the
alsa-driver tree, ``sound/pci`` directory in the case of PCI
cards.

In the following sections, the driver code is supposed to be put into
Linux kernel tree. The two cases are covered: a driver consisting of a
single source file and one consisting of several source files.

Driver with A Single Source File
--------------------------------

1. Modify sound/pci/Makefile

   Suppose you have a file xyz.c. Add the following two lines::

     snd-xyz-objs := xyz.o
     obj-$(CONFIG_SND_XYZ) += snd-xyz.o

2. Create the Kconfig entry

   Add the new entry of Kconfig for your xyz driver::

     config SND_XYZ
       tristate "Foobar XYZ"
       depends on SND
       select SND_PCM
       help
         Say Y here to include support for Foobar XYZ soundcard.
         To compile this driver as a module, choose M here:
         the module will be called snd-xyz.

The line ``select SND_PCM`` specifies that the driver xyz supports PCM.
In addition to SND_PCM, the following components are supported for
select command: SND_RAWMIDI, SND_TIMER, SND_HWDEP, SND_MPU401_UART,
SND_OPL3_LIB, SND_OPL4_LIB, SND_VX_LIB, SND_AC97_CODEC.
Add the select command for each supported component.

Note that some selections imply the lowlevel selections. For example,
PCM includes TIMER, MPU401_UART includes RAWMIDI, AC97_CODEC
includes PCM, and OPL3_LIB includes HWDEP. You don't need to give
the lowlevel selections again.

For the details of Kconfig script, refer to the kbuild documentation.

Drivers with Several Source Files
---------------------------------

Suppose that the driver snd-xyz have several source files. They are
located in the new subdirectory, sound/pci/xyz.

1. Add a new directory (``sound/pci/xyz``) in ``sound/pci/Makefile``
   as below::

     obj-$(CONFIG_SND) += sound/pci/xyz/


2. Under the directory ``sound/pci/xyz``, create a Makefile::

         snd-xyz-objs := xyz.o abc.o def.o
         obj-$(CONFIG_SND_XYZ) += snd-xyz.o

3. Create the Kconfig entry

   This procedure is as same as in the last section.


Useful Functions
================

:c:func:`snd_printk()` and friends
----------------------------------

.. note:: This subsection describes a few helper functions for
   decorating a bit more on the standard :c:func:`printk()` & co.
   However, in general, the use of such helpers is no longer recommended.
   If possible, try to stick with the standard functions like
   :c:func:`dev_err()` or :c:func:`pr_err()`.

ALSA provides a verbose version of the :c:func:`printk()` function.
If a kernel config ``CONFIG_SND_VERBOSE_PRINTK`` is set, this function
prints the given message together with the file name and the line of the
caller. The ``KERN_XXX`` prefix is processed as well as the original
:c:func:`printk()` does, so it's recommended to add this prefix,
e.g. snd_printk(KERN_ERR "Oh my, sorry, it's extremely bad!\\n");

There are also :c:func:`printk()`'s for debugging.
:c:func:`snd_printd()` can be used for general debugging purposes.
If ``CONFIG_SND_DEBUG`` is set, this function is compiled, and works
just like :c:func:`snd_printk()`. If the ALSA is compiled without
the debugging flag, it's ignored.

:c:func:`snd_printdd()` is compiled in only when
``CONFIG_SND_DEBUG_VERBOSE`` is set.

:c:func:`snd_BUG()`
-------------------

It shows the ``BUG?`` message and stack trace as well as
:c:func:`snd_BUG_ON()` at the point. It's useful to show that a
fatal error happens there.

When no debug flag is set, this macro is ignored.

:c:func:`snd_BUG_ON()`
----------------------

:c:func:`snd_BUG_ON()` macro is similar with
:c:func:`WARN_ON()` macro. For example, snd_BUG_ON(!pointer); or
it can be used as the condition, if (snd_BUG_ON(non_zero_is_bug))
return -EINVAL;

The macro takes an conditional expression to evaluate. When
``CONFIG_SND_DEBUG``, is set, if the expression is non-zero, it shows
the warning message such as ``BUG? (xxx)`` normally followed by stack
trace. In both cases it returns the evaluated value.

Acknowledgments
===============

I would like to thank Phil Kerr for his help for improvement and
corrections of this document.

Kevin Conder reformatted the original plain-text to the DocBook format.

Giuliano Pochini corrected typos and contributed the example codes in
the hardware constraints section.
