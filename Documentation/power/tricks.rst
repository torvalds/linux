================
swsusp/S3 tricks
================

Pavel Machek <pavel@ucw.cz>

If you want to trick swsusp/S3 into working, you might want to try:

* go with minimal config, turn off drivers like USB, AGP you don't
  really need

* turn off APIC and preempt

* use ext2. At least it has working fsck. [If something seems to go
  wrong, force fsck when you have a chance]

* turn off modules

* use vga text console, shut down X. [If you really want X, you might
  want to try vesafb later]

* try running as few processes as possible, preferably go to single
  user mode.

* due to video issues, swsusp should be easier to get working than
  S3. Try that first.

When you make it work, try to find out what exactly was it that broke
suspend, and preferably fix that.
