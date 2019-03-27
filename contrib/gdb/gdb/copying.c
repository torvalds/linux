/* ==> Do not modify this file!!  It is created automatically
   by copying.awk.  Modify copying.awk instead.  <== */

#include "defs.h"
#include "command.h"
#include "gdbcmd.h"

static void show_copying_command (char *, int);

static void show_warranty_command (char *, int);

void _initialize_copying (void);

extern int immediate_quit;
static void
show_copying_command (char *ignore, int from_tty)
{
  immediate_quit++;
  printf_filtered ("		    GNU GENERAL PUBLIC LICENSE\n");
  printf_filtered ("		       Version 2, June 1991\n");
  printf_filtered ("\n");
  printf_filtered (" Copyright (C) 1989, 1991 Free Software Foundation, Inc.\n");
  printf_filtered ("                       59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\n");
  printf_filtered (" Everyone is permitted to copy and distribute verbatim copies\n");
  printf_filtered (" of this license document, but changing it is not allowed.\n");
  printf_filtered ("\n");
  printf_filtered ("			    Preamble\n");
  printf_filtered ("\n");
  printf_filtered ("  The licenses for most software are designed to take away your\n");
  printf_filtered ("freedom to share and change it.  By contrast, the GNU General Public\n");
  printf_filtered ("License is intended to guarantee your freedom to share and change free\n");
  printf_filtered ("software--to make sure the software is free for all its users.  This\n");
  printf_filtered ("General Public License applies to most of the Free Software\n");
  printf_filtered ("Foundation's software and to any other program whose authors commit to\n");
  printf_filtered ("using it.  (Some other Free Software Foundation software is covered by\n");
  printf_filtered ("the GNU Library General Public License instead.)  You can apply it to\n");
  printf_filtered ("your programs, too.\n");
  printf_filtered ("\n");
  printf_filtered ("  When we speak of free software, we are referring to freedom, not\n");
  printf_filtered ("price.  Our General Public Licenses are designed to make sure that you\n");
  printf_filtered ("have the freedom to distribute copies of free software (and charge for\n");
  printf_filtered ("this service if you wish), that you receive source code or can get it\n");
  printf_filtered ("if you want it, that you can change the software or use pieces of it\n");
  printf_filtered ("in new free programs; and that you know you can do these things.\n");
  printf_filtered ("\n");
  printf_filtered ("  To protect your rights, we need to make restrictions that forbid\n");
  printf_filtered ("anyone to deny you these rights or to ask you to surrender the rights.\n");
  printf_filtered ("These restrictions translate to certain responsibilities for you if you\n");
  printf_filtered ("distribute copies of the software, or if you modify it.\n");
  printf_filtered ("\n");
  printf_filtered ("  For example, if you distribute copies of such a program, whether\n");
  printf_filtered ("gratis or for a fee, you must give the recipients all the rights that\n");
  printf_filtered ("you have.  You must make sure that they, too, receive or can get the\n");
  printf_filtered ("source code.  And you must show them these terms so they know their\n");
  printf_filtered ("rights.\n");
  printf_filtered ("\n");
  printf_filtered ("  We protect your rights with two steps: (1) copyright the software, and\n");
  printf_filtered ("(2) offer you this license which gives you legal permission to copy,\n");
  printf_filtered ("distribute and/or modify the software.\n");
  printf_filtered ("\n");
  printf_filtered ("  Also, for each author's protection and ours, we want to make certain\n");
  printf_filtered ("that everyone understands that there is no warranty for this free\n");
  printf_filtered ("software.  If the software is modified by someone else and passed on, we\n");
  printf_filtered ("want its recipients to know that what they have is not the original, so\n");
  printf_filtered ("that any problems introduced by others will not reflect on the original\n");
  printf_filtered ("authors' reputations.\n");
  printf_filtered ("\n");
  printf_filtered ("  Finally, any free program is threatened constantly by software\n");
  printf_filtered ("patents.  We wish to avoid the danger that redistributors of a free\n");
  printf_filtered ("program will individually obtain patent licenses, in effect making the\n");
  printf_filtered ("program proprietary.  To prevent this, we have made it clear that any\n");
  printf_filtered ("patent must be licensed for everyone's free use or not licensed at all.\n");
  printf_filtered ("\n");
  printf_filtered ("  The precise terms and conditions for copying, distribution and\n");
  printf_filtered ("modification follow.\n");
  printf_filtered ("\n");
  printf_filtered ("		    GNU GENERAL PUBLIC LICENSE\n");
  printf_filtered ("   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION\n");
  printf_filtered ("\n");
  printf_filtered ("  0. This License applies to any program or other work which contains\n");
  printf_filtered ("a notice placed by the copyright holder saying it may be distributed\n");
  printf_filtered ("under the terms of this General Public License.  The \"Program\", below,\n");
  printf_filtered ("refers to any such program or work, and a \"work based on the Program\"\n");
  printf_filtered ("means either the Program or any derivative work under copyright law:\n");
  printf_filtered ("that is to say, a work containing the Program or a portion of it,\n");
  printf_filtered ("either verbatim or with modifications and/or translated into another\n");
  printf_filtered ("language.  (Hereinafter, translation is included without limitation in\n");
  printf_filtered ("the term \"modification\".)  Each licensee is addressed as \"you\".\n");
  printf_filtered ("\n");
  printf_filtered ("Activities other than copying, distribution and modification are not\n");
  printf_filtered ("covered by this License; they are outside its scope.  The act of\n");
  printf_filtered ("running the Program is not restricted, and the output from the Program\n");
  printf_filtered ("is covered only if its contents constitute a work based on the\n");
  printf_filtered ("Program (independent of having been made by running the Program).\n");
  printf_filtered ("Whether that is true depends on what the Program does.\n");
  printf_filtered ("\n");
  printf_filtered ("  1. You may copy and distribute verbatim copies of the Program's\n");
  printf_filtered ("source code as you receive it, in any medium, provided that you\n");
  printf_filtered ("conspicuously and appropriately publish on each copy an appropriate\n");
  printf_filtered ("copyright notice and disclaimer of warranty; keep intact all the\n");
  printf_filtered ("notices that refer to this License and to the absence of any warranty;\n");
  printf_filtered ("and give any other recipients of the Program a copy of this License\n");
  printf_filtered ("along with the Program.\n");
  printf_filtered ("\n");
  printf_filtered ("You may charge a fee for the physical act of transferring a copy, and\n");
  printf_filtered ("you may at your option offer warranty protection in exchange for a fee.\n");
  printf_filtered ("\n");
  printf_filtered ("  2. You may modify your copy or copies of the Program or any portion\n");
  printf_filtered ("of it, thus forming a work based on the Program, and copy and\n");
  printf_filtered ("distribute such modifications or work under the terms of Section 1\n");
  printf_filtered ("above, provided that you also meet all of these conditions:\n");
  printf_filtered ("\n");
  printf_filtered ("    a) You must cause the modified files to carry prominent notices\n");
  printf_filtered ("    stating that you changed the files and the date of any change.\n");
  printf_filtered ("\n");
  printf_filtered ("    b) You must cause any work that you distribute or publish, that in\n");
  printf_filtered ("    whole or in part contains or is derived from the Program or any\n");
  printf_filtered ("    part thereof, to be licensed as a whole at no charge to all third\n");
  printf_filtered ("    parties under the terms of this License.\n");
  printf_filtered ("\n");
  printf_filtered ("    c) If the modified program normally reads commands interactively\n");
  printf_filtered ("    when run, you must cause it, when started running for such\n");
  printf_filtered ("    interactive use in the most ordinary way, to print or display an\n");
  printf_filtered ("    announcement including an appropriate copyright notice and a\n");
  printf_filtered ("    notice that there is no warranty (or else, saying that you provide\n");
  printf_filtered ("    a warranty) and that users may redistribute the program under\n");
  printf_filtered ("    these conditions, and telling the user how to view a copy of this\n");
  printf_filtered ("    License.  (Exception: if the Program itself is interactive but\n");
  printf_filtered ("    does not normally print such an announcement, your work based on\n");
  printf_filtered ("    the Program is not required to print an announcement.)\n");
  printf_filtered ("\n");
  printf_filtered ("These requirements apply to the modified work as a whole.  If\n");
  printf_filtered ("identifiable sections of that work are not derived from the Program,\n");
  printf_filtered ("and can be reasonably considered independent and separate works in\n");
  printf_filtered ("themselves, then this License, and its terms, do not apply to those\n");
  printf_filtered ("sections when you distribute them as separate works.  But when you\n");
  printf_filtered ("distribute the same sections as part of a whole which is a work based\n");
  printf_filtered ("on the Program, the distribution of the whole must be on the terms of\n");
  printf_filtered ("this License, whose permissions for other licensees extend to the\n");
  printf_filtered ("entire whole, and thus to each and every part regardless of who wrote it.\n");
  printf_filtered ("\n");
  printf_filtered ("Thus, it is not the intent of this section to claim rights or contest\n");
  printf_filtered ("your rights to work written entirely by you; rather, the intent is to\n");
  printf_filtered ("exercise the right to control the distribution of derivative or\n");
  printf_filtered ("collective works based on the Program.\n");
  printf_filtered ("\n");
  printf_filtered ("In addition, mere aggregation of another work not based on the Program\n");
  printf_filtered ("with the Program (or with a work based on the Program) on a volume of\n");
  printf_filtered ("a storage or distribution medium does not bring the other work under\n");
  printf_filtered ("the scope of this License.\n");
  printf_filtered ("\n");
  printf_filtered ("  3. You may copy and distribute the Program (or a work based on it,\n");
  printf_filtered ("under Section 2) in object code or executable form under the terms of\n");
  printf_filtered ("Sections 1 and 2 above provided that you also do one of the following:\n");
  printf_filtered ("\n");
  printf_filtered ("    a) Accompany it with the complete corresponding machine-readable\n");
  printf_filtered ("    source code, which must be distributed under the terms of Sections\n");
  printf_filtered ("    1 and 2 above on a medium customarily used for software interchange; or,\n");
  printf_filtered ("\n");
  printf_filtered ("    b) Accompany it with a written offer, valid for at least three\n");
  printf_filtered ("    years, to give any third party, for a charge no more than your\n");
  printf_filtered ("    cost of physically performing source distribution, a complete\n");
  printf_filtered ("    machine-readable copy of the corresponding source code, to be\n");
  printf_filtered ("    distributed under the terms of Sections 1 and 2 above on a medium\n");
  printf_filtered ("    customarily used for software interchange; or,\n");
  printf_filtered ("\n");
  printf_filtered ("    c) Accompany it with the information you received as to the offer\n");
  printf_filtered ("    to distribute corresponding source code.  (This alternative is\n");
  printf_filtered ("    allowed only for noncommercial distribution and only if you\n");
  printf_filtered ("    received the program in object code or executable form with such\n");
  printf_filtered ("    an offer, in accord with Subsection b above.)\n");
  printf_filtered ("\n");
  printf_filtered ("The source code for a work means the preferred form of the work for\n");
  printf_filtered ("making modifications to it.  For an executable work, complete source\n");
  printf_filtered ("code means all the source code for all modules it contains, plus any\n");
  printf_filtered ("associated interface definition files, plus the scripts used to\n");
  printf_filtered ("control compilation and installation of the executable.  However, as a\n");
  printf_filtered ("special exception, the source code distributed need not include\n");
  printf_filtered ("anything that is normally distributed (in either source or binary\n");
  printf_filtered ("form) with the major components (compiler, kernel, and so on) of the\n");
  printf_filtered ("operating system on which the executable runs, unless that component\n");
  printf_filtered ("itself accompanies the executable.\n");
  printf_filtered ("\n");
  printf_filtered ("If distribution of executable or object code is made by offering\n");
  printf_filtered ("access to copy from a designated place, then offering equivalent\n");
  printf_filtered ("access to copy the source code from the same place counts as\n");
  printf_filtered ("distribution of the source code, even though third parties are not\n");
  printf_filtered ("compelled to copy the source along with the object code.\n");
  printf_filtered ("\n");
  printf_filtered ("  4. You may not copy, modify, sublicense, or distribute the Program\n");
  printf_filtered ("except as expressly provided under this License.  Any attempt\n");
  printf_filtered ("otherwise to copy, modify, sublicense or distribute the Program is\n");
  printf_filtered ("void, and will automatically terminate your rights under this License.\n");
  printf_filtered ("However, parties who have received copies, or rights, from you under\n");
  printf_filtered ("this License will not have their licenses terminated so long as such\n");
  printf_filtered ("parties remain in full compliance.\n");
  printf_filtered ("\n");
  printf_filtered ("  5. You are not required to accept this License, since you have not\n");
  printf_filtered ("signed it.  However, nothing else grants you permission to modify or\n");
  printf_filtered ("distribute the Program or its derivative works.  These actions are\n");
  printf_filtered ("prohibited by law if you do not accept this License.  Therefore, by\n");
  printf_filtered ("modifying or distributing the Program (or any work based on the\n");
  printf_filtered ("Program), you indicate your acceptance of this License to do so, and\n");
  printf_filtered ("all its terms and conditions for copying, distributing or modifying\n");
  printf_filtered ("the Program or works based on it.\n");
  printf_filtered ("\n");
  printf_filtered ("  6. Each time you redistribute the Program (or any work based on the\n");
  printf_filtered ("Program), the recipient automatically receives a license from the\n");
  printf_filtered ("original licensor to copy, distribute or modify the Program subject to\n");
  printf_filtered ("these terms and conditions.  You may not impose any further\n");
  printf_filtered ("restrictions on the recipients' exercise of the rights granted herein.\n");
  printf_filtered ("You are not responsible for enforcing compliance by third parties to\n");
  printf_filtered ("this License.\n");
  printf_filtered ("\n");
  printf_filtered ("  7. If, as a consequence of a court judgment or allegation of patent\n");
  printf_filtered ("infringement or for any other reason (not limited to patent issues),\n");
  printf_filtered ("conditions are imposed on you (whether by court order, agreement or\n");
  printf_filtered ("otherwise) that contradict the conditions of this License, they do not\n");
  printf_filtered ("excuse you from the conditions of this License.  If you cannot\n");
  printf_filtered ("distribute so as to satisfy simultaneously your obligations under this\n");
  printf_filtered ("License and any other pertinent obligations, then as a consequence you\n");
  printf_filtered ("may not distribute the Program at all.  For example, if a patent\n");
  printf_filtered ("license would not permit royalty-free redistribution of the Program by\n");
  printf_filtered ("all those who receive copies directly or indirectly through you, then\n");
  printf_filtered ("the only way you could satisfy both it and this License would be to\n");
  printf_filtered ("refrain entirely from distribution of the Program.\n");
  printf_filtered ("\n");
  printf_filtered ("If any portion of this section is held invalid or unenforceable under\n");
  printf_filtered ("any particular circumstance, the balance of the section is intended to\n");
  printf_filtered ("apply and the section as a whole is intended to apply in other\n");
  printf_filtered ("circumstances.\n");
  printf_filtered ("\n");
  printf_filtered ("It is not the purpose of this section to induce you to infringe any\n");
  printf_filtered ("patents or other property right claims or to contest validity of any\n");
  printf_filtered ("such claims; this section has the sole purpose of protecting the\n");
  printf_filtered ("integrity of the free software distribution system, which is\n");
  printf_filtered ("implemented by public license practices.  Many people have made\n");
  printf_filtered ("generous contributions to the wide range of software distributed\n");
  printf_filtered ("through that system in reliance on consistent application of that\n");
  printf_filtered ("system; it is up to the author/donor to decide if he or she is willing\n");
  printf_filtered ("to distribute software through any other system and a licensee cannot\n");
  printf_filtered ("impose that choice.\n");
  printf_filtered ("\n");
  printf_filtered ("This section is intended to make thoroughly clear what is believed to\n");
  printf_filtered ("be a consequence of the rest of this License.\n");
  printf_filtered ("\n");
  printf_filtered ("  8. If the distribution and/or use of the Program is restricted in\n");
  printf_filtered ("certain countries either by patents or by copyrighted interfaces, the\n");
  printf_filtered ("original copyright holder who places the Program under this License\n");
  printf_filtered ("may add an explicit geographical distribution limitation excluding\n");
  printf_filtered ("those countries, so that distribution is permitted only in or among\n");
  printf_filtered ("countries not thus excluded.  In such case, this License incorporates\n");
  printf_filtered ("the limitation as if written in the body of this License.\n");
  printf_filtered ("\n");
  printf_filtered ("  9. The Free Software Foundation may publish revised and/or new versions\n");
  printf_filtered ("of the General Public License from time to time.  Such new versions will\n");
  printf_filtered ("be similar in spirit to the present version, but may differ in detail to\n");
  printf_filtered ("address new problems or concerns.\n");
  printf_filtered ("\n");
  printf_filtered ("Each version is given a distinguishing version number.  If the Program\n");
  printf_filtered ("specifies a version number of this License which applies to it and \"any\n");
  printf_filtered ("later version\", you have the option of following the terms and conditions\n");
  printf_filtered ("either of that version or of any later version published by the Free\n");
  printf_filtered ("Software Foundation.  If the Program does not specify a version number of\n");
  printf_filtered ("this License, you may choose any version ever published by the Free Software\n");
  printf_filtered ("Foundation.\n");
  printf_filtered ("\n");
  printf_filtered ("  10. If you wish to incorporate parts of the Program into other free\n");
  printf_filtered ("programs whose distribution conditions are different, write to the author\n");
  printf_filtered ("to ask for permission.  For software which is copyrighted by the Free\n");
  printf_filtered ("Software Foundation, write to the Free Software Foundation; we sometimes\n");
  printf_filtered ("make exceptions for this.  Our decision will be guided by the two goals\n");
  printf_filtered ("of preserving the free status of all derivatives of our free software and\n");
  printf_filtered ("of promoting the sharing and reuse of software generally.\n");
  printf_filtered ("\n");
  immediate_quit--;
}

static void
show_warranty_command (char *ignore, int from_tty)
{
  immediate_quit++;
  printf_filtered ("			    NO WARRANTY\n");
  printf_filtered ("\n");
  printf_filtered ("  11. BECAUSE THE PROGRAM IS LICENSED FREE OF CHARGE, THERE IS NO WARRANTY\n");
  printf_filtered ("FOR THE PROGRAM, TO THE EXTENT PERMITTED BY APPLICABLE LAW.  EXCEPT WHEN\n");
  printf_filtered ("OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES\n");
  printf_filtered ("PROVIDE THE PROGRAM \"AS IS\" WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED\n");
  printf_filtered ("OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF\n");
  printf_filtered ("MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE ENTIRE RISK AS\n");
  printf_filtered ("TO THE QUALITY AND PERFORMANCE OF THE PROGRAM IS WITH YOU.  SHOULD THE\n");
  printf_filtered ("PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF ALL NECESSARY SERVICING,\n");
  printf_filtered ("REPAIR OR CORRECTION.\n");
  printf_filtered ("\n");
  printf_filtered ("  12. IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING\n");
  printf_filtered ("WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MAY MODIFY AND/OR\n");
  printf_filtered ("REDISTRIBUTE THE PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR DAMAGES,\n");
  printf_filtered ("INCLUDING ANY GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING\n");
  printf_filtered ("OUT OF THE USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED\n");
  printf_filtered ("TO LOSS OF DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY\n");
  printf_filtered ("YOU OR THIRD PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY OTHER\n");
  printf_filtered ("PROGRAMS), EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE\n");
  printf_filtered ("POSSIBILITY OF SUCH DAMAGES.\n");
  printf_filtered ("\n");
  immediate_quit--;
}

void
_initialize_copying (void)
{
  add_cmd ("copying", no_class, show_copying_command,
	   "Conditions for redistributing copies of GDB.",
	   &showlist);
  add_cmd ("warranty", no_class, show_warranty_command,
	   "Various kinds of warranty you do not have.",
	   &showlist);

  /* For old-timers, allow "info copying", etc.  */
  add_info ("copying", show_copying_command,
	    "Conditions for redistributing copies of GDB.");
  add_info ("warranty", show_warranty_command,
	    "Various kinds of warranty you do not have.");
}
