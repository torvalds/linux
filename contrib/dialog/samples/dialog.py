#!/usr/bin/python
# $Id: dialog.py,v 1.4 2012/06/29 09:33:18 tom Exp $
# Module: dialog.py
# Copyright (c) 2000 Robb Shecter <robb@acm.org>
# All rights reserved.
# This source is covered by the GNU GPL.
#
# This module is a Python wrapper around the Linux "dialog" utility
# by Savio Lam and Stuart Herbert.  My goals were to make dialog as
# easy to use from Python as possible.  The demo code at the end of
# the module is a good example of how to use it.  To run the demo,
# execute:
#
#                       python dialog.py
#
# This module has one class in it, "Dialog".  An application typically
# creates an instance of it, and possibly sets the background title option.
# Then, methods can be called on it for interacting with the user.
#
# I wrote this because I want to use my 486-33 laptop as my main
# development computer (!), and I wanted a way to nicely interact with the
# user in console mode.  There are apparently other modules out there
# with similar functionality, but they require the Python curses library.
# Writing this module from scratch was easier than figuring out how to
# recompile Python with curses enabled. :)
#
# One interesting feature is that the menu and selection windows allow
# *any* objects to be displayed and selected, not just strings.
#
# TO DO:
#   Add code so that the input buffer is flushed before a dialog box is
#     shown.  This would make the UI more predictable for users.  This
#     feature could be turned on and off through an instance method.
#   Drop using temporary files when interacting with 'dialog'
#     (it's possible -- I've already tried :-).
#   Try detecting the terminal window size in order to make reasonable
#     height and width defaults.  Hmmm - should also then check for 
#     terminal resizing...
#   Put into a package name to make more reusable - reduce the possibility
#     of name collisions.
#
# NOTES:
#         there is a bug in (at least) Linux-Mandrake 7.0 Russian Edition
#         running on AMD K6-2 3D that causes core dump when 'dialog' 
#         is running with --gauge option;
#         in this case you'll have to recompile 'dialog' program.
#
# Modifications:
# Jul 2000, Sultanbek Tezadov (http://sultan.da.ru)
#    Added:
#       - 'gauge' widget *)
#       - 'title' option to some widgets
#       - 'checked' option to checklist dialog; clicking "Cancel" is now
#           recognizable
#       - 'selected' option to radiolist dialog; clicking "Cancel" is now
#           recognizable
#       - some other cosmetic changes and improvements
#   

import os
from tempfile import mktemp
from string import split
from time import sleep

#
# Path of the dialog executable
#
DIALOG = os.getenv("DIALOG");
if DIALOG is None:
	DIALOG="../dialog";

class Dialog:
    def __init__(self):
	self.__bgTitle = ''               # Default is no background title


    def setBackgroundTitle(self, text):
	self.__bgTitle = '--backtitle "%s"' % text


    def __perform(self, cmd):
	"""Do the actual work of invoking dialog and getting the output."""
	fName = mktemp()
	rv = os.system('%s %s %s 2> %s' % (DIALOG, self.__bgTitle, cmd, fName))
	f = open(fName)
	output = f.readlines()
	f.close()
	os.unlink(fName)
	return (rv, output)


    def __perform_no_options(self, cmd):
	"""Call dialog w/out passing any more options. Needed by --clear."""
	return os.system(DIALOG + ' ' + cmd)


    def __handleTitle(self, title):
	if len(title) == 0:
	    return ''
	else:
	    return '--title "%s" ' % title


    def yesno(self, text, height=10, width=30, title=''):
	"""
	Put a Yes/No question to the user.
	Uses the dialog --yesno option.
	Returns a 1 or a 0.
	"""
	(code, output) = self.__perform(self.__handleTitle(title) +\
	    '--yesno "%s" %d %d' % (text, height, width))
	return code == 0


    def msgbox(self, text, height=10, width=30, title=''):
	"""
	Pop up a message to the user which has to be clicked
	away with "ok".
	"""
	self.__perform(self.__handleTitle(title) +\
	    '--msgbox "%s" %d %d' % (text, height, width))


    def infobox(self, text, height=10, width=30):
	"""Make a message to the user, and return immediately."""
	self.__perform('--infobox "%s" %d %d' % (text, height, width))


    def inputbox(self, text, height=10, width=30, init='', title=''):
	"""
	Request a line of input from the user.
	Returns the user's input or None if cancel was chosen.
	"""
	(c, o) = self.__perform(self.__handleTitle(title) +\
	    '--inputbox "%s" %d %d "%s"' % (text, height, width, init))
	try:
	    return o[0]
	except IndexError:
	    if c == 0:  # empty string entered
		return ''
	    else:  # canceled
		return None


    def textbox(self, filename, height=20, width=60, title=None):
	"""Display a file in a scrolling text box."""
	if title is None:
	    title = filename
	self.__perform(self.__handleTitle(title) +\
	    ' --textbox "%s" %d %d' % (filename, height, width))


    def menu(self, text, height=15, width=54, list=[]):
	"""
	Display a menu of options to the user.  This method simplifies the
	--menu option of dialog, which allows for complex arguments.  This
	method receives a simple list of objects, and each one is assigned
	a choice number.
	The selected object is returned, or None if the dialog was canceled.
	"""
	menuheight = height - 8
	pairs = map(lambda i, item: (i + 1, item), range(len(list)), list)
	choices = reduce(lambda res, pair: res + '%d "%s" ' % pair, pairs, '')
	(code, output) = self.__perform('--menu "%s" %d %d %d %s' %\
	    (text, height, width, menuheight, choices))
	try:
	    return list[int(output[0]) - 1]
	except IndexError:
	    return None


    def checklist(self, text, height=15, width=54, list=[], checked=None):
	"""
	Returns a list of the selected objects.
	Returns an empty list if nothing was selected.
	Returns None if the window was canceled.
	checked -- a list of boolean (0/1) values; len(checked) must equal 
	    len(list).
	"""
	if checked is None:
	    checked = [0]*len(list)
	menuheight = height - 8
	triples = map(
	    lambda i, item, onoff, fs=('off', 'on'): (i + 1, item, fs[onoff]),
	    range(len(list)), list, checked)
	choices = reduce(lambda res, triple: res + '%d "%s" %s ' % triple,
	    triples, '')
	(c, o) = self.__perform('--checklist "%s" %d %d %d %s' %\
	    (text, height, width, menuheight, choices))
	try:
	    output = o[0]
	    indexList  = map(lambda x: int(x[1:-1]), split(output))
	    objectList = filter(lambda item, list=list, indexList=indexList: 
		    list.index(item) + 1 in indexList,
		list)
	    return objectList
	except IndexError:
	    if c == 0:                        # Nothing was selected
		return []
	    return None  # Was canceled


    def radiolist(self, text, height=15, width=54, list=[], selected=0):
	"""
	Return the selected object.
	Returns empty string if no choice was selected.
	Returns None if window was canceled.
	selected -- the selected item (must be between 1 and len(list)
	    or 0, meaning no selection).
	"""
	menuheight = height - 8
	triples = map(lambda i, item: (i + 1, item, 'off'),
	    range(len(list)), list)
	if selected:
	    i, item, tmp = triples[selected - 1]
	    triples[selected - 1] = (i, item, 'on')
	choices = reduce(lambda res, triple: res + '%d "%s" %s ' % triple,
	    triples, '')
	(c, o) = self.__perform('--radiolist "%s" %d %d %d %s' %\
	    (text, height, width, menuheight, choices))
	try:
	    return list[int(o[0]) - 1]
	except IndexError:
	    if c == 0:
		return ''
	    return None
 

    def clear(self):
	"""
	Clear the screen. Equivalent to the dialog --clear option.
	"""
	self.__perform_no_options('--clear')


    def scrollbox(self, text, height=20, width=60, title=''):
	"""
	This is a bonus method.  The dialog package only has a function to
	display a file in a scrolling text field.  This method allows any
	string to be displayed by first saving it in a temp file, and calling
	--textbox.
	"""
	fName = mktemp()
	f = open(fName, 'w')
	f.write(text)
	f.close()
	self.__perform(self.__handleTitle(title) +\
	    '--textbox "%s" %d %d' % (fName, height, width))
	os.unlink(fName)


    def gauge_start(self, perc=0, text='', height=8, width=54, title=''):
	"""
	Display gauge output window.
	Gauge normal usage (assuming that there is an instace of 'Dialog'
	class named 'd'):
	    d.gauge_start()
	    # do something
	    d.gauge_iterate(10)  # passed throgh 10%
	    # ...
	    d.gauge_iterate(100, 'any text here')  # work is done
	    d.stop_gauge()  # clean-up actions
	"""
	cmd = self.__handleTitle(title) +\
	    '--gauge "%s" %d %d %d' % (text, height, width, perc)
	cmd = '%s %s %s 2> /dev/null' % (DIALOG, self.__bgTitle, cmd)
	self.pipe = os.popen(cmd, 'w')
    #/gauge_start()


    def gauge_iterate(self, perc, text=''):
	"""
	Update percentage point value.
	
	See gauge_start() function above for the usage.
	"""
	if text:
	    text = 'XXX\n%d\n%s\nXXX\n' % (perc, text)
	else:
	    text = '%d\n' % perc
	self.pipe.write(text)
	self.pipe.flush()
    #/gauge_iterate()
    
    
    def gauge_stop(self):
	"""
	Finish previously started gauge.
	
	See gauge_start() function above for the usage.
	"""
	self.pipe.close()
    #/gauge_stop()



#
# DEMO APPLICATION
#
if __name__ == '__main__':
    """
    This demo tests all the features of the class.
    """
    d = Dialog()
    d.setBackgroundTitle('dialog.py demo')

    d.infobox(
	"One moment... Just wasting some time here to test the infobox...")
    sleep(3)

    if d.yesno("Do you like this demo?"):
	d.msgbox("Excellent!  Here's the source code:")
    else:
	d.msgbox("Send your complaints to /dev/null")
    
    d.textbox("dialog.py")

    name = d.inputbox("What's your name?", init="Snow White")
    fday = d.menu("What's your favorite day of the week?", 
	list=["Monday", "Tuesday", "Wednesday", "Thursday", 
	    "Friday (The best day of all)", "Saturday", "Sunday"])
    food = d.checklist("What sandwich toppings do you like?", 
	list=["Catsup", "Mustard", "Pesto", "Mayonaise", "Horse radish", 
	    "Sun-dried tomatoes"], checked=[0,0,0,1,1,1])
    sand = d.radiolist("What's your favorite kind of sandwich?", 
	list=["Hamburger", "Hotdog", "Burrito", "Doener", "Falafel", 
	    "Bagel", "Big Mac", "Whopper", "Quarter Pounder", 
	    "Peanut Butter and Jelly", "Grilled cheese"], selected=4)

    # Prepare the message for the final window
    bigMessage = "Here are some vital statistics about you:\n\nName: " + name +\
        "\nFavorite day of the week: " + fday +\
	"\nFavorite sandwich toppings:\n"
    for topping in food:
	bigMessage = bigMessage + "    " + topping + "\n"
    bigMessage = bigMessage + "Favorite sandwich: " + str(sand)

    d.scrollbox(bigMessage)

    #<>#  Gauge Demo
    d.gauge_start(0, 'percentage: 0', title='Gauge Demo')
    for i in range(1, 101):
	if i < 50:
	    msg = 'percentage: %d' % i
	elif i == 50:
	    msg = 'Over 50%'
	else:
	    msg = ''
	d.gauge_iterate(i, msg)
	sleep(0.1)
    d.gauge_stop()
    #<>#

    d.clear()
