;; Itanium2 DFA descriptions for insn scheduling and bundling.
;; Copyright (C) 2002, 2004, 2005 Free Software Foundation, Inc.
;; Contributed by Vladimir Makarov <vmakarov@redhat.com>.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.  */
;;

/* This is description of pipeline hazards based on DFA.  The
   following constructions can be used for this:
   
   o define_cpu_unit string [string]) describes a cpu functional unit
     (separated by comma).

     1st operand: Names of cpu function units.
     2nd operand: Name of automaton (see comments for
     DEFINE_AUTOMATON).

     All define_reservations and define_cpu_units should have unique
     names which cannot be "nothing".

   o (exclusion_set string string) means that each CPU function unit
     in the first string cannot be reserved simultaneously with each
     unit whose name is in the second string and vise versa.  CPU
     units in the string are separated by commas. For example, it is
     useful for description CPU with fully pipelined floating point
     functional unit which can execute simultaneously only single
     floating point insns or only double floating point insns.

   o (presence_set string string) means that each CPU function unit in
     the first string cannot be reserved unless at least one of
     pattern of units whose names are in the second string is
     reserved.  This is an asymmetric relation.  CPU units or unit
     patterns in the strings are separated by commas.  Pattern is one
     unit name or unit names separated by white-spaces.
 
     For example, it is useful for description that slot1 is reserved
     after slot0 reservation for a VLIW processor.  We could describe
     it by the following construction

         (presence_set "slot1" "slot0")

     Or slot1 is reserved only after slot0 and unit b0 reservation.
     In this case we could write

         (presence_set "slot1" "slot0 b0")

     All CPU functional units in a set should belong to the same
     automaton.

   o (final_presence_set string string) is analogous to
     `presence_set'.  The difference between them is when checking is
     done.  When an instruction is issued in given automaton state
     reflecting all current and planned unit reservations, the
     automaton state is changed.  The first state is a source state,
     the second one is a result state.  Checking for `presence_set' is
     done on the source state reservation, checking for
     `final_presence_set' is done on the result reservation.  This
     construction is useful to describe a reservation which is
     actually two subsequent reservations.  For example, if we use

         (presence_set "slot1" "slot0")

     the following insn will be never issued (because slot1 requires
     slot0 which is absent in the source state).

         (define_reservation "insn_and_nop" "slot0 + slot1")

     but it can be issued if we use analogous `final_presence_set'.

   o (absence_set string string) means that each CPU function unit in
     the first string can be reserved only if each pattern of units
     whose names are in the second string is not reserved.  This is an
     asymmetric relation (actually exclusion set is analogous to this
     one but it is symmetric).  CPU units or unit patterns in the
     string are separated by commas.  Pattern is one unit name or unit
     names separated by white-spaces.

     For example, it is useful for description that slot0 cannot be
     reserved after slot1 or slot2 reservation for a VLIW processor.
     We could describe it by the following construction

        (absence_set "slot2" "slot0, slot1")

     Or slot2 cannot be reserved if slot0 and unit b0 are reserved or
     slot1 and unit b1 are reserved .  In this case we could write

        (absence_set "slot2" "slot0 b0, slot1 b1")

     All CPU functional units in a set should to belong the same
     automaton.

   o (final_absence_set string string) is analogous to `absence_set' but
     checking is done on the result (state) reservation.  See comments
     for final_presence_set.

   o (define_bypass number out_insn_names in_insn_names) names bypass with
     given latency (the first number) from insns given by the first
     string (see define_insn_reservation) into insns given by the
     second string.  Insn names in the strings are separated by
     commas.

   o (define_automaton string) describes names of an automaton
     generated and used for pipeline hazards recognition.  The names
     are separated by comma.  Actually it is possibly to generate the
     single automaton but unfortunately it can be very large.  If we
     use more one automata, the summary size of the automata usually
     is less than the single one.  The automaton name is used in
     define_cpu_unit.  All automata should have unique names.

   o (automata_option string) describes option for generation of
     automata.  Currently there are the following options:

     o "no-minimization" which makes no minimization of automata.
       This is only worth to do when we are debugging the description
       and need to look more accurately at reservations of states.

     o "ndfa" which makes automata with nondetermenistic reservation
        by insns.

   o (define_reservation string string) names reservation (the first
     string) of cpu functional units (the 2nd string).  Sometimes unit
     reservations for different insns contain common parts.  In such
     case, you describe common part and use one its name (the 1st
     parameter) in regular expression in define_insn_reservation.  All
     define_reservations, define results and define_cpu_units should
     have unique names which cannot be "nothing".

   o (define_insn_reservation name default_latency condition regexpr)
     describes reservation of cpu functional units (the 3nd operand)
     for instruction which is selected by the condition (the 2nd
     parameter).  The first parameter is used for output of debugging
     information.  The reservations are described by a regular
     expression according the following syntax:

       regexp = regexp "," oneof
              | oneof

       oneof = oneof "|" allof
             | allof

       allof = allof "+" repeat
             | repeat
 
       repeat = element "*" number
              | element

       element = cpu_function_name
               | reservation_name
               | result_name
               | "nothing"
               | "(" regexp ")"

       1. "," is used for describing start of the next cycle in
          reservation.

       2. "|" is used for describing the reservation described by the
          first regular expression *or* the reservation described by
          the second regular expression *or* etc.

       3. "+" is used for describing the reservation described by the
          first regular expression *and* the reservation described by
          the second regular expression *and* etc.

       4. "*" is used for convenience and simply means sequence in
          which the regular expression are repeated NUMBER times with
          cycle advancing (see ",").

       5. cpu function unit name which means reservation.

       6. reservation name -- see define_reservation.

       7. string "nothing" means no units reservation.

*/

(define_automaton "two")

;;   All possible combinations of bundles/syllables
(define_cpu_unit "2_0m.ii, 2_0m.mi, 2_0m.fi, 2_0m.mf, 2_0b.bb, 2_0m.bb,\
                  2_0m.ib, 2_0m.mb, 2_0m.fb, 2_0m.lx" "two")
(define_cpu_unit "2_0mi.i, 2_0mm.i, 2_0mf.i, 2_0mm.f, 2_0bb.b, 2_0mb.b,\
                  2_0mi.b, 2_0mm.b, 2_0mf.b, 2_0mlx." "two")
(define_cpu_unit "2_0mii., 2_0mmi., 2_0mfi., 2_0mmf., 2_0bbb., 2_0mbb.,\
                  2_0mib., 2_0mmb., 2_0mfb." "two")

(define_cpu_unit "2_1m.ii, 2_1m.mi, 2_1m.fi, 2_1m.mf, 2_1b.bb, 2_1m.bb,\
                  2_1m.ib, 2_1m.mb, 2_1m.fb, 2_1m.lx" "two")
(define_cpu_unit "2_1mi.i, 2_1mm.i, 2_1mf.i, 2_1mm.f, 2_1bb.b, 2_1mb.b,\
                  2_1mi.b, 2_1mm.b, 2_1mf.b, 2_1mlx." "two")
(define_cpu_unit "2_1mii., 2_1mmi., 2_1mfi., 2_1mmf., 2_1bbb., 2_1mbb.,\
                  2_1mib., 2_1mmb., 2_1mfb." "two")

;; Slot 1
(exclusion_set "2_0m.ii" "2_0m.mi, 2_0m.fi, 2_0m.mf, 2_0b.bb, 2_0m.bb,\
                          2_0m.ib, 2_0m.mb, 2_0m.fb, 2_0m.lx")
(exclusion_set "2_0m.mi" "2_0m.fi, 2_0m.mf, 2_0b.bb, 2_0m.bb, 2_0m.ib,\
                          2_0m.mb, 2_0m.fb, 2_0m.lx")
(exclusion_set "2_0m.fi" "2_0m.mf, 2_0b.bb, 2_0m.bb, 2_0m.ib, 2_0m.mb,\
                          2_0m.fb, 2_0m.lx")
(exclusion_set "2_0m.mf" "2_0b.bb, 2_0m.bb, 2_0m.ib, 2_0m.mb, 2_0m.fb,\
	                  2_0m.lx")
(exclusion_set "2_0b.bb" "2_0m.bb, 2_0m.ib, 2_0m.mb, 2_0m.fb, 2_0m.lx")
(exclusion_set "2_0m.bb" "2_0m.ib, 2_0m.mb, 2_0m.fb, 2_0m.lx")
(exclusion_set "2_0m.ib" "2_0m.mb, 2_0m.fb, 2_0m.lx")
(exclusion_set "2_0m.mb" "2_0m.fb, 2_0m.lx")
(exclusion_set "2_0m.fb" "2_0m.lx")

;; Slot 2
(exclusion_set "2_0mi.i" "2_0mm.i, 2_0mf.i, 2_0mm.f, 2_0bb.b, 2_0mb.b,\
                          2_0mi.b, 2_0mm.b, 2_0mf.b, 2_0mlx.")
(exclusion_set "2_0mm.i" "2_0mf.i, 2_0mm.f, 2_0bb.b, 2_0mb.b,\
                          2_0mi.b, 2_0mm.b, 2_0mf.b, 2_0mlx.")
(exclusion_set "2_0mf.i" "2_0mm.f, 2_0bb.b, 2_0mb.b, 2_0mi.b, 2_0mm.b,\
                          2_0mf.b, 2_0mlx.")
(exclusion_set "2_0mm.f" "2_0bb.b, 2_0mb.b, 2_0mi.b, 2_0mm.b, 2_0mf.b,\
                          2_0mlx.")
(exclusion_set "2_0bb.b" "2_0mb.b, 2_0mi.b, 2_0mm.b, 2_0mf.b, 2_0mlx.")
(exclusion_set "2_0mb.b" "2_0mi.b, 2_0mm.b, 2_0mf.b, 2_0mlx.")
(exclusion_set "2_0mi.b" "2_0mm.b, 2_0mf.b, 2_0mlx.")
(exclusion_set "2_0mm.b" "2_0mf.b, 2_0mlx.")
(exclusion_set "2_0mf.b" "2_0mlx.")

;; Slot 3
(exclusion_set "2_0mii." "2_0mmi., 2_0mfi., 2_0mmf., 2_0bbb., 2_0mbb.,\
                          2_0mib., 2_0mmb., 2_0mfb., 2_0mlx.")
(exclusion_set "2_0mmi." "2_0mfi., 2_0mmf., 2_0bbb., 2_0mbb.,\
                          2_0mib., 2_0mmb., 2_0mfb., 2_0mlx.")
(exclusion_set "2_0mfi." "2_0mmf., 2_0bbb., 2_0mbb., 2_0mib., 2_0mmb.,\
                          2_0mfb., 2_0mlx.")
(exclusion_set "2_0mmf." "2_0bbb., 2_0mbb., 2_0mib., 2_0mmb., 2_0mfb.,\
                          2_0mlx.")
(exclusion_set "2_0bbb." "2_0mbb., 2_0mib., 2_0mmb., 2_0mfb., 2_0mlx.")
(exclusion_set "2_0mbb." "2_0mib., 2_0mmb., 2_0mfb., 2_0mlx.")
(exclusion_set "2_0mib." "2_0mmb., 2_0mfb., 2_0mlx.")
(exclusion_set "2_0mmb." "2_0mfb., 2_0mlx.")
(exclusion_set "2_0mfb." "2_0mlx.")

;; Slot 4
(exclusion_set "2_1m.ii" "2_1m.mi, 2_1m.fi, 2_1m.mf, 2_1b.bb, 2_1m.bb,\
                          2_1m.ib, 2_1m.mb, 2_1m.fb, 2_1m.lx")
(exclusion_set "2_1m.mi" "2_1m.fi, 2_1m.mf, 2_1b.bb, 2_1m.bb, 2_1m.ib,\
                          2_1m.mb, 2_1m.fb, 2_1m.lx")
(exclusion_set "2_1m.fi" "2_1m.mf, 2_1b.bb, 2_1m.bb, 2_1m.ib, 2_1m.mb,\
                          2_1m.fb, 2_1m.lx")
(exclusion_set "2_1m.mf" "2_1b.bb, 2_1m.bb, 2_1m.ib, 2_1m.mb, 2_1m.fb,\
                          2_1m.lx")
(exclusion_set "2_1b.bb" "2_1m.bb, 2_1m.ib, 2_1m.mb, 2_1m.fb, 2_1m.lx")
(exclusion_set "2_1m.bb" "2_1m.ib, 2_1m.mb, 2_1m.fb, 2_1m.lx")
(exclusion_set "2_1m.ib" "2_1m.mb, 2_1m.fb, 2_1m.lx")
(exclusion_set "2_1m.mb" "2_1m.fb, 2_1m.lx")
(exclusion_set "2_1m.fb" "2_1m.lx")

;; Slot 5
(exclusion_set "2_1mi.i" "2_1mm.i, 2_1mf.i, 2_1mm.f, 2_1bb.b, 2_1mb.b,\
                          2_1mi.b, 2_1mm.b, 2_1mf.b, 2_1mlx.")
(exclusion_set "2_1mm.i" "2_1mf.i, 2_1mm.f, 2_1bb.b, 2_1mb.b,\
                          2_1mi.b, 2_1mm.b, 2_1mf.b, 2_1mlx.")
(exclusion_set "2_1mf.i" "2_1mm.f, 2_1bb.b, 2_1mb.b, 2_1mi.b, 2_1mm.b,\
                          2_1mf.b, 2_1mlx.")
(exclusion_set "2_1mm.f" "2_1bb.b, 2_1mb.b, 2_1mi.b, 2_1mm.b, 2_1mf.b,\
                          2_1mlx.")
(exclusion_set "2_1bb.b" "2_1mb.b, 2_1mi.b, 2_1mm.b, 2_1mf.b, 2_1mlx.")
(exclusion_set "2_1mb.b" "2_1mi.b, 2_1mm.b, 2_1mf.b, 2_1mlx.")
(exclusion_set "2_1mi.b" "2_1mm.b, 2_1mf.b, 2_1mlx.")
(exclusion_set "2_1mm.b" "2_1mf.b, 2_1mlx.")
(exclusion_set "2_1mf.b" "2_1mlx.")

;; Slot 6
(exclusion_set "2_1mii." "2_1mmi., 2_1mfi., 2_1mmf., 2_1bbb., 2_1mbb.,\
                          2_1mib., 2_1mmb., 2_1mfb., 2_1mlx.")
(exclusion_set "2_1mmi." "2_1mfi., 2_1mmf., 2_1bbb., 2_1mbb.,\
                          2_1mib., 2_1mmb., 2_1mfb., 2_1mlx.")
(exclusion_set "2_1mfi." "2_1mmf., 2_1bbb., 2_1mbb., 2_1mib., 2_1mmb.,\
                          2_1mfb., 2_1mlx.")
(exclusion_set "2_1mmf." "2_1bbb., 2_1mbb., 2_1mib., 2_1mmb., 2_1mfb.,\
                          2_1mlx.")
(exclusion_set "2_1bbb." "2_1mbb., 2_1mib., 2_1mmb., 2_1mfb., 2_1mlx.")
(exclusion_set "2_1mbb." "2_1mib., 2_1mmb., 2_1mfb., 2_1mlx.")
(exclusion_set "2_1mib." "2_1mmb., 2_1mfb., 2_1mlx.")
(exclusion_set "2_1mmb." "2_1mfb., 2_1mlx.")
(exclusion_set "2_1mfb." "2_1mlx.")

(final_presence_set "2_0mi.i" "2_0m.ii")
(final_presence_set "2_0mii." "2_0mi.i")
(final_presence_set "2_1mi.i" "2_1m.ii")
(final_presence_set "2_1mii." "2_1mi.i")

(final_presence_set "2_0mm.i" "2_0m.mi")
(final_presence_set "2_0mmi." "2_0mm.i")
(final_presence_set "2_1mm.i" "2_1m.mi")
(final_presence_set "2_1mmi." "2_1mm.i")

(final_presence_set "2_0mf.i" "2_0m.fi")
(final_presence_set "2_0mfi." "2_0mf.i")
(final_presence_set "2_1mf.i" "2_1m.fi")
(final_presence_set "2_1mfi." "2_1mf.i")

(final_presence_set "2_0mm.f" "2_0m.mf")
(final_presence_set "2_0mmf." "2_0mm.f")
(final_presence_set "2_1mm.f" "2_1m.mf")
(final_presence_set "2_1mmf." "2_1mm.f")

(final_presence_set "2_0bb.b" "2_0b.bb")
(final_presence_set "2_0bbb." "2_0bb.b")
(final_presence_set "2_1bb.b" "2_1b.bb")
(final_presence_set "2_1bbb." "2_1bb.b")

(final_presence_set "2_0mb.b" "2_0m.bb")
(final_presence_set "2_0mbb." "2_0mb.b")
(final_presence_set "2_1mb.b" "2_1m.bb")
(final_presence_set "2_1mbb." "2_1mb.b")

(final_presence_set "2_0mi.b" "2_0m.ib")
(final_presence_set "2_0mib." "2_0mi.b")
(final_presence_set "2_1mi.b" "2_1m.ib")
(final_presence_set "2_1mib." "2_1mi.b")

(final_presence_set "2_0mm.b" "2_0m.mb")
(final_presence_set "2_0mmb." "2_0mm.b")
(final_presence_set "2_1mm.b" "2_1m.mb")
(final_presence_set "2_1mmb." "2_1mm.b")

(final_presence_set "2_0mf.b" "2_0m.fb")
(final_presence_set "2_0mfb." "2_0mf.b")
(final_presence_set "2_1mf.b" "2_1m.fb")
(final_presence_set "2_1mfb." "2_1mf.b")

(final_presence_set "2_0mlx." "2_0m.lx")
(final_presence_set "2_1mlx." "2_1m.lx")

;;   The following reflects the dual issue bundle types table.
;;   We could place all possible combinations here because impossible
;; combinations would go away by the subsequent constrains.
(final_presence_set
   "2_1m.lx"
   "2_0mmi.,2_0mfi.,2_0mmf.,2_0mib.,2_0mmb.,2_0mfb.,2_0mlx.")
(final_presence_set "2_1b.bb" "2_0mii.,2_0mmi.,2_0mfi.,2_0mmf.,2_0mlx.")
(final_presence_set
   "2_1m.ii,2_1m.mi,2_1m.fi,2_1m.mf,2_1m.bb,2_1m.ib,2_1m.mb,2_1m.fb"
   "2_0mii.,2_0mmi.,2_0mfi.,2_0mmf.,2_0mib.,2_0mmb.,2_0mfb.,2_0mlx.")

;;  Ports/units (nb means nop.b insn issued into given port):
(define_cpu_unit
   "2_um0, 2_um1, 2_um2, 2_um3, 2_ui0, 2_ui1, 2_uf0, 2_uf1,\
    2_ub0, 2_ub1, 2_ub2, 2_unb0, 2_unb1, 2_unb2" "two")

(exclusion_set "2_ub0" "2_unb0")
(exclusion_set "2_ub1" "2_unb1")
(exclusion_set "2_ub2" "2_unb2")

;; The following rules are used to decrease number of alternatives.
;; They are consequences of Itanium2 microarchitecture.  They also
;; describe the following rules mentioned in Itanium2
;; microarchitecture: rules mentioned in Itanium2 microarchitecture:
;; o "BBB/MBB: Always splits issue after either of these bundles".
;; o "MIB BBB: Split issue after the first bundle in this pair".
(exclusion_set
   "2_0b.bb,2_0bb.b,2_0bbb.,2_0m.bb,2_0mb.b,2_0mbb."
   "2_1m.ii,2_1m.mi,2_1m.fi,2_1m.mf,2_1b.bb,2_1m.bb,\
    2_1m.ib,2_1m.mb,2_1m.fb,2_1m.lx")
(exclusion_set "2_0m.ib,2_0mi.b,2_0mib." "2_1b.bb")

;;; "MIB/MFB/MMB: Splits issue after any of these bundles unless the
;;; B-slot contains a nop.b or a brp instruction".
;;;   "The B in an MIB/MFB/MMB bundle disperses to B0 if it is a brp or
;;; nop.b, otherwise it disperses to B2".
(final_absence_set
   "2_1m.ii, 2_1m.mi, 2_1m.fi, 2_1m.mf, 2_1b.bb, 2_1m.bb,\
    2_1m.ib, 2_1m.mb, 2_1m.fb, 2_1m.lx"
   "2_0mib. 2_ub2, 2_0mfb. 2_ub2, 2_0mmb. 2_ub2")

;; This is necessary to start new processor cycle when we meet stop bit.
(define_cpu_unit "2_stop" "two")
(final_absence_set
   "2_0m.ii,2_0mi.i,2_0mii.,2_0m.mi,2_0mm.i,2_0mmi.,2_0m.fi,2_0mf.i,2_0mfi.,\
    2_0m.mf,2_0mm.f,2_0mmf.,2_0b.bb,2_0bb.b,2_0bbb.,2_0m.bb,2_0mb.b,2_0mbb.,\
    2_0m.ib,2_0mi.b,2_0mib.,2_0m.mb,2_0mm.b,2_0mmb.,2_0m.fb,2_0mf.b,2_0mfb.,\
    2_0m.lx,2_0mlx., \
    2_1m.ii,2_1mi.i,2_1mii.,2_1m.mi,2_1mm.i,2_1mmi.,2_1m.fi,2_1mf.i,2_1mfi.,\
    2_1m.mf,2_1mm.f,2_1mmf.,2_1b.bb,2_1bb.b,2_1bbb.,2_1m.bb,2_1mb.b,2_1mbb.,\
    2_1m.ib,2_1mi.b,2_1mib.,2_1m.mb,2_1mm.b,2_1mmb.,2_1m.fb,2_1mf.b,2_1mfb.,\
    2_1m.lx,2_1mlx."
   "2_stop")

;;   The issue logic can reorder M slot insns between different subtypes
;; but cannot reorder insn within the same subtypes.  The following
;; constraint is enough to describe this.
(final_presence_set "2_um1" "2_um0")
(final_presence_set "2_um3" "2_um2")

;;   The insn in the 1st I slot of the two bundle issue group will issue
;; to I0.  The second I slot insn will issue to I1.
(final_presence_set "2_ui1" "2_ui0")

;;  For exceptions of I insns:
(define_cpu_unit "2_only_ui0" "two")
(final_absence_set "2_only_ui0"  "2_ui1")

;; Insns

(define_reservation "2_M0"
  "(2_0m.ii|2_0m.mi|2_0m.fi|2_0m.mf|2_0m.bb|2_0m.ib|2_0m.mb|2_0m.fb|2_0m.lx\
    |2_1m.ii|2_1m.mi|2_1m.fi|2_1m.mf|2_1m.bb|2_1m.ib|2_1m.mb|2_1m.fb|2_1m.lx\
    |2_0mm.i|2_0mm.f|2_0mm.b|2_1mm.i|2_1mm.f|2_1mm.b)\
   +(2_um0|2_um1|2_um2|2_um3)")

(define_reservation "2_M1"
  "(2_0mii.+(2_ui0|2_ui1)|2_0mmi.+2_ui0|2_0mfi.+2_ui0|2_0mmf.+2_uf0\
    |2_0mib.+2_unb0|2_0mfb.+2_unb0|2_0mmb.+2_unb0)\
   +(2_1m.ii|2_1m.mi|2_1m.fi|2_1m.mf|2_1m.bb|2_1m.ib|2_1m.mb|2_1m.fb|2_1m.lx)\
   +(2_um0|2_um1|2_um2|2_um3)")

(define_reservation "2_M" "2_M0|2_M1")

(define_reservation "2_M0_only_um0"
  "(2_0m.ii|2_0m.mi|2_0m.fi|2_0m.mf|2_0m.bb|2_0m.ib|2_0m.mb|2_0m.fb|2_0m.lx\
    |2_1m.ii|2_1m.mi|2_1m.fi|2_1m.mf|2_1m.bb|2_1m.ib|2_1m.mb|2_1m.fb|2_1m.lx\
    |2_0mm.i|2_0mm.f|2_0mm.b|2_1mm.i|2_1mm.f|2_1mm.b)\
   +2_um0")

(define_reservation "2_M1_only_um0"
  "(2_0mii.+(2_ui0|2_ui1)|2_0mmi.+2_ui0|2_0mfi.+2_ui0|2_0mmf.+2_uf0\
    |2_0mib.+2_unb0|2_0mfb.+2_unb0|2_0mmb.+2_unb0)\
   +(2_1m.ii|2_1m.mi|2_1m.fi|2_1m.mf|2_1m.bb|2_1m.ib|2_1m.mb|2_1m.fb|2_1m.lx)\
   +2_um0")

(define_reservation "2_M_only_um0" "2_M0_only_um0|2_M1_only_um0")

(define_reservation "2_M0_only_um2"
  "(2_0m.ii|2_0m.mi|2_0m.fi|2_0m.mf|2_0m.bb|2_0m.ib|2_0m.mb|2_0m.fb|2_0m.lx\
    |2_1m.ii|2_1m.mi|2_1m.fi|2_1m.mf|2_1m.bb|2_1m.ib|2_1m.mb|2_1m.fb|2_1m.lx\
    |2_0mm.i|2_0mm.f|2_0mm.b|2_1mm.i|2_1mm.f|2_1mm.b)\
   +2_um2")

(define_reservation "2_M1_only_um2"
  "(2_0mii.+(2_ui0|2_ui1)|2_0mmi.+2_ui0|2_0mfi.+2_ui0|2_0mmf.+2_uf0\
    |2_0mib.+2_unb0|2_0mfb.+2_unb0|2_0mmb.+2_unb0)\
   +(2_1m.ii|2_1m.mi|2_1m.fi|2_1m.mf|2_1m.bb|2_1m.ib|2_1m.mb|2_1m.fb|2_1m.lx)\
   +2_um2")

(define_reservation "2_M_only_um2" "2_M0_only_um2|2_M1_only_um2")

(define_reservation "2_M0_only_um23"
  "(2_0m.ii|2_0m.mi|2_0m.fi|2_0m.mf|2_0m.bb|2_0m.ib|2_0m.mb|2_0m.fb|2_0m.lx\
    |2_1m.ii|2_1m.mi|2_1m.fi|2_1m.mf|2_1m.bb|2_1m.ib|2_1m.mb|2_1m.fb|2_1m.lx\
    |2_0mm.i|2_0mm.f|2_0mm.b|2_1mm.i|2_1mm.f|2_1mm.b)\
   +(2_um2|2_um3)")

(define_reservation "2_M1_only_um23"
  "(2_0mii.+(2_ui0|2_ui1)|2_0mmi.+2_ui0|2_0mfi.+2_ui0|2_0mmf.+2_uf0\
    |2_0mib.+2_unb0|2_0mfb.+2_unb0|2_0mmb.+2_unb0)\
   +(2_1m.ii|2_1m.mi|2_1m.fi|2_1m.mf|2_1m.bb|2_1m.ib|2_1m.mb|2_1m.fb|2_1m.lx)\
   +(2_um2|2_um3)")

(define_reservation "2_M_only_um23" "2_M0_only_um23|2_M1_only_um23")

(define_reservation "2_M0_only_um01"
  "(2_0m.ii|2_0m.mi|2_0m.fi|2_0m.mf|2_0m.bb|2_0m.ib|2_0m.mb|2_0m.fb|2_0m.lx\
    |2_1m.ii|2_1m.mi|2_1m.fi|2_1m.mf|2_1m.bb|2_1m.ib|2_1m.mb|2_1m.fb|2_1m.lx\
    |2_0mm.i|2_0mm.f|2_0mm.b|2_1mm.i|2_1mm.f|2_1mm.b)\
   +(2_um0|2_um1)")

(define_reservation "2_M1_only_um01"
  "(2_0mii.+(2_ui0|2_ui1)|2_0mmi.+2_ui0|2_0mfi.+2_ui0|2_0mmf.+2_uf0\
    |2_0mib.+2_unb0|2_0mfb.+2_unb0|2_0mmb.+2_unb0)\
   +(2_1m.ii|2_1m.mi|2_1m.fi|2_1m.mf|2_1m.bb|2_1m.ib|2_1m.mb|2_1m.fb|2_1m.lx)\
   +(2_um0|2_um1)")

(define_reservation "2_M_only_um01" "2_M0_only_um01|2_M1_only_um01")

;; I instruction is dispersed to the lowest numbered I unit
;; not already in use.  Remember about possible splitting.
(define_reservation "2_I0"
  "2_0mi.i+2_ui0|2_0mii.+(2_ui0|2_ui1)|2_0mmi.+2_ui0\
   |2_0mfi.+2_ui0|2_0mi.b+2_ui0|(2_1mi.i|2_1mi.b)+(2_ui0|2_ui1)\
   |(2_1mii.|2_1mmi.|2_1mfi.)+(2_ui0|2_ui1)")

(define_reservation "2_I1"
  "2_0m.ii+(2_um0|2_um1|2_um2|2_um3)+2_0mi.i+2_ui0\
   |2_0mm.i+(2_um0|2_um1|2_um2|2_um3)+2_0mmi.+2_ui0\
   |2_0mf.i+2_uf0+2_0mfi.+2_ui0\
   |2_0m.ib+(2_um0|2_um1|2_um2|2_um3)+2_0mi.b+2_ui0\
   |(2_1m.ii+2_1mi.i|2_1m.ib+2_1mi.b)+(2_um0|2_um1|2_um2|2_um3)+(2_ui0|2_ui1)\
   |2_1mm.i+(2_um0|2_um1|2_um2|2_um3)+2_1mmi.+(2_ui0|2_ui1)\
   |2_1mf.i+2_uf1+2_1mfi.+(2_ui0|2_ui1)")

(define_reservation "2_I" "2_I0|2_I1")

;; "An F slot in the 1st bundle disperses to F0".
;; "An F slot in the 2st bundle disperses to F1".
(define_reservation "2_F0"
   "2_0mf.i+2_uf0|2_0mmf.+2_uf0|2_0mf.b+2_uf0\
    |2_1mf.i+2_uf1|2_1mmf.+2_uf1|2_1mf.b+2_uf1")

(define_reservation "2_F1"
   "(2_0m.fi+2_0mf.i|2_0mm.f+2_0mmf.|2_0m.fb+2_0mf.b)\
    +(2_um0|2_um1|2_um2|2_um3)+2_uf0\
    |(2_1m.fi+2_1mf.i|2_1mm.f+2_1mmf.|2_1m.fb+2_1mf.b)\
     +(2_um0|2_um1|2_um2|2_um3)+2_uf1")

(define_reservation "2_F2"
   "(2_0m.mf+2_0mm.f+2_0mmf.+2_uf0|2_1m.mf+2_1mm.f+2_1mmf.+2_uf1)\
    +(2_um0|2_um1|2_um2|2_um3)+(2_um0|2_um1|2_um2|2_um3)\
    |(2_0mii.+(2_ui0|2_ui1)|2_0mmi.+2_ui0|2_0mfi.+2_ui0\
      |2_0mmf.+(2_um0|2_um1|2_um2|2_um3)\
      |2_0mib.+2_unb0|2_0mmb.+2_unb0|2_0mfb.+2_unb0)\
     +(2_1m.fi+2_1mf.i|2_1m.fb+2_1mf.b)+(2_um0|2_um1|2_um2|2_um3)+2_uf1")

(define_reservation "2_F" "2_F0|2_F1|2_F2")

;;; "Each B slot in MBB or BBB bundle disperses to the corresponding B
;;; unit. That is, a B slot in 1st position is dispersed to B0.  In the
;;; 2nd position it is dispersed to B2".
(define_reservation "2_NB"
    "2_0b.bb+2_unb0|2_0bb.b+2_unb1|2_0bbb.+2_unb2\
     |2_0mb.b+2_unb1|2_0mbb.+2_unb2|2_0mib.+2_unb0\
     |2_0mmb.+2_unb0|2_0mfb.+2_unb0\
     |2_1b.bb+2_unb0|2_1bb.b+2_unb1
     |2_1bbb.+2_unb2|2_1mb.b+2_unb1|2_1mbb.+2_unb2\
     |2_1mib.+2_unb0|2_1mmb.+2_unb0|2_1mfb.+2_unb0")

(define_reservation "2_B0"
   "2_0b.bb+2_ub0|2_0bb.b+2_ub1|2_0bbb.+2_ub2\
    |2_0mb.b+2_ub1|2_0mbb.+2_ub2|2_0mib.+2_ub2\
    |2_0mfb.+2_ub2|2_1b.bb+2_ub0|2_1bb.b+2_ub1\
    |2_1bbb.+2_ub2|2_1mb.b+2_ub1\
    |2_1mib.+2_ub2|2_1mmb.+2_ub2|2_1mfb.+2_ub2")

(define_reservation "2_B1"
   "2_0m.bb+(2_um0|2_um1|2_um2|2_um3)+2_0mb.b+2_ub1\
    |2_0mi.b+2_ui0+2_0mib.+2_ub2\
    |2_0mm.b+(2_um0|2_um1|2_um2|2_um3)+2_0mmb.+2_ub2\
    |2_0mf.b+2_uf0+2_0mfb.+2_ub2\
    |(2_0mii.+(2_ui0|2_ui1)|2_0mmi.+2_ui0|2_0mfi.+2_ui0|2_0mmf.+2_uf0)\
     +2_1b.bb+2_ub0\
    |2_1m.bb+(2_um0|2_um1|2_um2|2_um3)+2_1mb.b+2_ub1\
    |2_1mi.b+(2_ui0|2_ui1)+2_1mib.+2_ub2\
    |2_1mm.b+(2_um0|2_um1|2_um2|2_um3)+2_1mmb.+2_ub2\
    |2_1mf.b+2_uf1+2_1mfb.+2_ub2")

(define_reservation "2_B" "2_B0|2_B1")

;; MLX bunlde uses ports equivalent to MFI bundles.

;;   For the MLI template, the I slot insn is always assigned to port I0
;; if it is in the first bundle or it is assigned to port I1 if it is in
;; the second bundle.
(define_reservation "2_L0" "2_0mlx.+2_ui0+2_uf0|2_1mlx.+2_ui1+2_uf1")

(define_reservation "2_L1"
   "2_0m.lx+(2_um0|2_um1|2_um2|2_um3)+2_0mlx.+2_ui0+2_uf0\
   |2_1m.lx+(2_um0|2_um1|2_um2|2_um3)+2_1mlx.+2_ui1+2_uf1")

(define_reservation "2_L2"
   "(2_0mii.+(2_ui0|2_ui1)|2_0mmi.+2_ui0|2_0mfi.+2_ui0|2_0mmf.+2_uf0\
     |2_0mib.+2_unb0|2_0mmb.+2_unb0|2_0mfb.+2_unb0)
    +2_1m.lx+(2_um0|2_um1|2_um2|2_um3)+2_1mlx.+2_ui1+2_uf1")

(define_reservation "2_L" "2_L0|2_L1|2_L2")

;;   Should we describe that A insn in I slot can be issued into M
;; ports?  I think it is not necessary because of multipass
;; scheduling.  For example, the multipass scheduling could use
;; MMI-MMI instead of MII-MII where the two last I slots contain A
;; insns (even if the case is complicated by use-def conflicts).
;;
;; In any case we could describe it as
;;    (define_cpu_unit "2_ui1_0pres,2_ui1_1pres,2_ui1_2pres,2_ui1_3pres" "two")
;;    (final_presence_set "2_ui1_0pres,2_ui1_1pres,2_ui1_2pres,2_ui1_3pres"
;;                        "2_ui1")
;;    (define_reservation "b_A"
;;       "b_M|b_I\
;;        |(2_1mi.i|2_1mii.|2_1mmi.|2_1mfi.|2_1mi.b)+(2_um0|2_um1|2_um2|2_um3)\
;;         +(2_ui1_0pres|2_ui1_1pres|2_ui1_2pres|2_ui1_3pres)")

(define_reservation "2_A" "2_M|2_I")

;; We assume that there is no insn issued on the same cycle as the
;; unknown insn.
(define_cpu_unit "2_empty" "two")
(exclusion_set "2_empty"
    "2_0m.ii,2_0m.mi,2_0m.fi,2_0m.mf,2_0b.bb,2_0m.bb,2_0m.ib,2_0m.mb,2_0m.fb,\
     2_0m.lx")

(define_cpu_unit
   "2_0m_bs, 2_0mi_bs, 2_0mm_bs, 2_0mf_bs, 2_0b_bs, 2_0bb_bs, 2_0mb_bs"
   "two")
(define_cpu_unit
   "2_1m_bs, 2_1mi_bs, 2_1mm_bs, 2_1mf_bs, 2_1b_bs, 2_1bb_bs, 2_1mb_bs"
   "two")

(define_cpu_unit "2_m_cont, 2_mi_cont, 2_mm_cont, 2_mf_cont, 2_mb_cont,\
	          2_b_cont, 2_bb_cont" "two")

;; For stop in the middle of the bundles.
(define_cpu_unit "2_m_stop, 2_m0_stop, 2_m1_stop, 2_0mmi_cont" "two")
(define_cpu_unit "2_mi_stop, 2_mi0_stop, 2_mi1_stop, 2_0mii_cont" "two")

(final_presence_set "2_0m_bs"
   "2_0m.ii, 2_0m.mi, 2_0m.mf, 2_0m.fi, 2_0m.bb,\
    2_0m.ib, 2_0m.fb, 2_0m.mb, 2_0m.lx")
(final_presence_set "2_1m_bs"
   "2_1m.ii, 2_1m.mi, 2_1m.mf, 2_1m.fi, 2_1m.bb,\
    2_1m.ib, 2_1m.fb, 2_1m.mb, 2_1m.lx")
(final_presence_set "2_0mi_bs"  "2_0mi.i, 2_0mi.i")
(final_presence_set "2_1mi_bs"  "2_1mi.i, 2_1mi.i")
(final_presence_set "2_0mm_bs"  "2_0mm.i, 2_0mm.f, 2_0mm.b")
(final_presence_set "2_1mm_bs"  "2_1mm.i, 2_1mm.f, 2_1mm.b")
(final_presence_set "2_0mf_bs"  "2_0mf.i, 2_0mf.b")
(final_presence_set "2_1mf_bs"  "2_1mf.i, 2_1mf.b")
(final_presence_set "2_0b_bs"  "2_0b.bb")
(final_presence_set "2_1b_bs"  "2_1b.bb")
(final_presence_set "2_0bb_bs"  "2_0bb.b")
(final_presence_set "2_1bb_bs"  "2_1bb.b")
(final_presence_set "2_0mb_bs"  "2_0mb.b")
(final_presence_set "2_1mb_bs"  "2_1mb.b")

(exclusion_set "2_0m_bs"
   "2_0mi.i, 2_0mm.i, 2_0mm.f, 2_0mf.i, 2_0mb.b,\
    2_0mi.b, 2_0mf.b, 2_0mm.b, 2_0mlx., 2_m0_stop")
(exclusion_set "2_1m_bs"
   "2_1mi.i, 2_1mm.i, 2_1mm.f, 2_1mf.i, 2_1mb.b,\
    2_1mi.b, 2_1mf.b, 2_1mm.b, 2_1mlx., 2_m1_stop")
(exclusion_set "2_0mi_bs"  "2_0mii., 2_0mib., 2_mi0_stop")
(exclusion_set "2_1mi_bs"  "2_1mii., 2_1mib., 2_mi1_stop")
(exclusion_set "2_0mm_bs"  "2_0mmi., 2_0mmf., 2_0mmb.")
(exclusion_set "2_1mm_bs"  "2_1mmi., 2_1mmf., 2_1mmb.")
(exclusion_set "2_0mf_bs"  "2_0mfi., 2_0mfb.")
(exclusion_set "2_1mf_bs"  "2_1mfi., 2_1mfb.")
(exclusion_set "2_0b_bs"  "2_0bb.b")
(exclusion_set "2_1b_bs"  "2_1bb.b")
(exclusion_set "2_0bb_bs"  "2_0bbb.")
(exclusion_set "2_1bb_bs"  "2_1bbb.")
(exclusion_set "2_0mb_bs"  "2_0mbb.")
(exclusion_set "2_1mb_bs"  "2_1mbb.")

(exclusion_set
   "2_0m_bs, 2_0mi_bs, 2_0mm_bs, 2_0mf_bs, 2_0b_bs, 2_0bb_bs, 2_0mb_bs,
    2_1m_bs, 2_1mi_bs, 2_1mm_bs, 2_1mf_bs, 2_1b_bs, 2_1bb_bs, 2_1mb_bs"
   "2_stop")

(final_presence_set
   "2_0mi.i, 2_0mm.i, 2_0mf.i, 2_0mm.f, 2_0mb.b,\
    2_0mi.b, 2_0mm.b, 2_0mf.b, 2_0mlx."
   "2_m_cont")
(final_presence_set "2_0mii., 2_0mib." "2_mi_cont")
(final_presence_set "2_0mmi., 2_0mmf., 2_0mmb." "2_mm_cont")
(final_presence_set "2_0mfi., 2_0mfb." "2_mf_cont")
(final_presence_set "2_0bb.b" "2_b_cont")
(final_presence_set "2_0bbb." "2_bb_cont")
(final_presence_set "2_0mbb." "2_mb_cont")

(exclusion_set
   "2_0m.ii, 2_0m.mi, 2_0m.fi, 2_0m.mf, 2_0b.bb, 2_0m.bb,\
    2_0m.ib, 2_0m.mb, 2_0m.fb, 2_0m.lx"
   "2_m_cont, 2_mi_cont, 2_mm_cont, 2_mf_cont,\
    2_mb_cont, 2_b_cont, 2_bb_cont")

(exclusion_set "2_empty"
               "2_m_cont,2_mi_cont,2_mm_cont,2_mf_cont,\
                2_mb_cont,2_b_cont,2_bb_cont")

;; For m;mi bundle
(final_presence_set "2_m0_stop" "2_0m.mi")
(final_presence_set "2_0mm.i" "2_0mmi_cont")
(exclusion_set "2_0mmi_cont"
   "2_0m.ii, 2_0m.mi, 2_0m.fi, 2_0m.mf, 2_0b.bb, 2_0m.bb,\
    2_0m.ib, 2_0m.mb, 2_0m.fb, 2_0m.lx")
(exclusion_set "2_m0_stop" "2_0mm.i")
(final_presence_set "2_m1_stop" "2_1m.mi")
(exclusion_set "2_m1_stop" "2_1mm.i")
(final_presence_set "2_m_stop" "2_m0_stop, 2_m1_stop")

;; For mi;i bundle
(final_presence_set "2_mi0_stop" "2_0mi.i")
(final_presence_set "2_0mii." "2_0mii_cont")
(exclusion_set "2_0mii_cont"
   "2_0m.ii, 2_0m.mi, 2_0m.fi, 2_0m.mf, 2_0b.bb, 2_0m.bb,\
    2_0m.ib, 2_0m.mb, 2_0m.fb, 2_0m.lx")
(exclusion_set "2_mi0_stop" "2_0mii.")
(final_presence_set "2_mi1_stop" "2_1mi.i")
(exclusion_set "2_mi1_stop" "2_1mii.")
(final_presence_set "2_mi_stop" "2_mi0_stop, 2_mi1_stop")

(final_absence_set
   "2_0m.ii,2_0mi.i,2_0mii.,2_0m.mi,2_0mm.i,2_0mmi.,2_0m.fi,2_0mf.i,2_0mfi.,\
    2_0m.mf,2_0mm.f,2_0mmf.,2_0b.bb,2_0bb.b,2_0bbb.,2_0m.bb,2_0mb.b,2_0mbb.,\
    2_0m.ib,2_0mi.b,2_0mib.,2_0m.mb,2_0mm.b,2_0mmb.,2_0m.fb,2_0mf.b,2_0mfb.,\
    2_0m.lx,2_0mlx., \
    2_1m.ii,2_1mi.i,2_1mii.,2_1m.mi,2_1mm.i,2_1mmi.,2_1m.fi,2_1mf.i,2_1mfi.,\
    2_1m.mf,2_1mm.f,2_1mmf.,2_1b.bb,2_1bb.b,2_1bbb.,2_1m.bb,2_1mb.b,2_1mbb.,\
    2_1m.ib,2_1mi.b,2_1mib.,2_1m.mb,2_1mm.b,2_1mmb.,2_1m.fb,2_1mf.b,2_1mfb.,\
    2_1m.lx,2_1mlx."
   "2_m0_stop,2_m1_stop,2_mi0_stop,2_mi1_stop")

(define_insn_reservation "2_stop_bit" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "stop_bit"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_stop|2_m0_stop|2_m1_stop|2_mi0_stop|2_mi1_stop")

(define_insn_reservation "2_br"      0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "br"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_B")
(define_insn_reservation "2_scall"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "scall"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_B")
(define_insn_reservation "2_fcmp"    2
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "fcmp"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_F")
(define_insn_reservation "2_fcvtfx"  4
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "fcvtfx"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_F")
(define_insn_reservation "2_fld"     6
  (and (and (and (and (eq_attr "cpu" "itanium2")
                      (eq_attr "itanium_class" "fld"))
                 (eq_attr "data_speculative" "no"))
            (eq_attr "check_load" "no"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M")
(define_insn_reservation "2_flda"    6
  (and (and (and (eq_attr "cpu" "itanium2")
                 (eq_attr "itanium_class" "fld"))
            (eq_attr "data_speculative" "yes"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um01")
(define_insn_reservation "2_fldc"    0
  (and (and (and (eq_attr "cpu" "itanium2")
                 (eq_attr "itanium_class" "fld"))
            (eq_attr "check_load" "yes"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um01")

(define_insn_reservation "2_fldp"    6
  (and (and (and (eq_attr "cpu" "itanium2")
		 (eq_attr "itanium_class" "fldp"))
	    (eq_attr "check_load" "no"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um01")
(define_insn_reservation "2_fldpc"   0
  (and (and (and (eq_attr "cpu" "itanium2")
		 (eq_attr "itanium_class" "fldp"))
	    (eq_attr "check_load" "yes"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um01")

(define_insn_reservation "2_fmac"    4
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "fmac"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_F")
(define_insn_reservation "2_fmisc"   4
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "fmisc"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_F")

;; There is only one insn `mov = ar.bsp' for frar_i:
;; Latency time ???
(define_insn_reservation "2_frar_i" 13
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "frar_i"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_I+2_only_ui0")
;; There is only two insns `mov = ar.unat' or `mov = ar.ccv' for frar_m:
;; Latency time ???
(define_insn_reservation "2_frar_m"  6
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "frar_m"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um2")
(define_insn_reservation "2_frbr"    2
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "frbr"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_I+2_only_ui0")
(define_insn_reservation "2_frfr"    5
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "frfr"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um2")
(define_insn_reservation "2_frpr"    2
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "frpr"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_I+2_only_ui0")

(define_insn_reservation "2_ialu"      1
    (and (and (eq_attr "cpu" "itanium2")
              (eq_attr "itanium_class" "ialu"))
         (eq (symbol_ref "bundling_p") (const_int 0)))
    "2_A")
(define_insn_reservation "2_icmp"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "icmp"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_A")
(define_insn_reservation "2_ilog"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "ilog"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_A")
(define_insn_reservation "2_mmalua"  2
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "mmalua"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_A")
;; Latency time ???
(define_insn_reservation "2_ishf"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "ishf"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_I+2_only_ui0")

(define_insn_reservation "2_ld"      1
  (and (and (and (eq_attr "cpu" "itanium2")
                 (eq_attr "itanium_class" "ld"))
            (eq_attr "check_load" "no"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um01")
(define_insn_reservation "2_ldc"     0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "check_load" "yes"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um01")

(define_insn_reservation "2_long_i"  1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "long_i"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_L")

(define_insn_reservation "2_mmmul"   2
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "mmmul"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_I+2_only_ui0")
;; Latency time ???
(define_insn_reservation "2_mmshf"   2
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "mmshf"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_I")
;; Latency time ???
(define_insn_reservation "2_mmshfi"  1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "mmshfi"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_I")

;; Now we have only one insn (flushrs) of such class.  We assume that flushrs
;; is the 1st syllable of the bundle after stop bit.
(define_insn_reservation "2_rse_m"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "rse_m"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "(2_0m.ii|2_0m.mi|2_0m.fi|2_0m.mf|2_0m.bb\
    |2_0m.ib|2_0m.mb|2_0m.fb|2_0m.lx)+2_um0")
(define_insn_reservation "2_sem"     0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "sem"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um23")

(define_insn_reservation "2_stf"     1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "stf"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um23")
(define_insn_reservation "2_st"      1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "st"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um23")
(define_insn_reservation "2_syst_m0" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "syst_m0"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um2")
(define_insn_reservation "2_syst_m"  0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "syst_m"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um0")
;; Reservation???
(define_insn_reservation "2_tbit"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "tbit"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_I+2_only_ui0")

;; There is only ony insn `mov ar.pfs =' for toar_i:
(define_insn_reservation "2_toar_i"  0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "toar_i"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_I+2_only_ui0")
;; There are only ony 2 insns `mov ar.ccv =' and `mov ar.unat =' for toar_m:
;; Latency time ???
(define_insn_reservation "2_toar_m"  5
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "toar_m"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um2")
;; Latency time ???
(define_insn_reservation "2_tobr"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "tobr"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_I+2_only_ui0")
(define_insn_reservation "2_tofr"    5
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "tofr"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um23")
;; Latency time ???
(define_insn_reservation "2_topr"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "topr"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_I+2_only_ui0")

(define_insn_reservation "2_xmpy"    4
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "xmpy"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_F")
;; Latency time ???
(define_insn_reservation "2_xtd"     1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "xtd"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_I")

(define_insn_reservation "2_chk_s_i" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "chk_s_i"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_I|2_M_only_um23")
(define_insn_reservation "2_chk_s_f" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "chk_s_f"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um23")
(define_insn_reservation "2_chk_a"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "chk_a"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um01")

(define_insn_reservation "2_lfetch"  0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "lfetch"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M_only_um01")

(define_insn_reservation "2_nop_m"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop_m"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_M0")
(define_insn_reservation "2_nop_b"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop_b"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_NB")
(define_insn_reservation "2_nop_i"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop_i"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_I0")
(define_insn_reservation "2_nop_f"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop_f"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_F0")
(define_insn_reservation "2_nop_x"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop_x"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_L0")

(define_insn_reservation "2_unknown" 1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "unknown"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "2_empty")

(define_insn_reservation "2_nop" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop"))
       (eq (symbol_ref "bundling_p") (const_int 0)))
  "2_M0|2_NB|2_I0|2_F0")

(define_insn_reservation "2_ignore" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "ignore"))
       (eq (symbol_ref "bundling_p") (const_int 0))) "nothing")

(define_cpu_unit "2_m_cont_only, 2_b_cont_only" "two")
(define_cpu_unit "2_mi_cont_only, 2_mm_cont_only, 2_mf_cont_only" "two")
(define_cpu_unit "2_mb_cont_only, 2_bb_cont_only" "two")

(final_presence_set "2_m_cont_only" "2_m_cont")
(exclusion_set "2_m_cont_only"
  "2_0mi.i, 2_0mm.i, 2_0mf.i, 2_0mm.f, 2_0mb.b,\
   2_0mi.b, 2_0mm.b, 2_0mf.b, 2_0mlx.")

(final_presence_set "2_b_cont_only" "2_b_cont")
(exclusion_set "2_b_cont_only"  "2_0bb.b")

(final_presence_set "2_mi_cont_only" "2_mi_cont")
(exclusion_set "2_mi_cont_only" "2_0mii., 2_0mib.")

(final_presence_set "2_mm_cont_only" "2_mm_cont")
(exclusion_set "2_mm_cont_only" "2_0mmi., 2_0mmf., 2_0mmb.")

(final_presence_set "2_mf_cont_only" "2_mf_cont")
(exclusion_set "2_mf_cont_only" "2_0mfi., 2_0mfb.")

(final_presence_set "2_mb_cont_only" "2_mb_cont")
(exclusion_set "2_mb_cont_only" "2_0mbb.")

(final_presence_set "2_bb_cont_only" "2_bb_cont")
(exclusion_set "2_bb_cont_only" "2_0bbb.")

(define_insn_reservation "2_pre_cycle" 0
   (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "pre_cycle"))
        (eq (symbol_ref "bundling_p") (const_int 0)))
                         "nothing")

;;(define_insn_reservation "2_pre_cycle" 0
;;   (and (and (eq_attr "cpu" "itanium2")
;;             (eq_attr "itanium_class" "pre_cycle"))
;;        (eq (symbol_ref "bundling_p") (const_int 0)))
;;                         "(2_0m_bs, 2_m_cont)                     \
;;                          | (2_0mi_bs, (2_mi_cont|nothing))       \
;;                          | (2_0mm_bs, 2_mm_cont)                 \
;;                          | (2_0mf_bs, (2_mf_cont|nothing))       \
;;                          | (2_0b_bs, (2_b_cont|nothing))         \
;;                          | (2_0bb_bs, (2_bb_cont|nothing))       \
;;                          | (2_0mb_bs, (2_mb_cont|nothing))       \
;;                          | (2_1m_bs, 2_m_cont)                   \
;;                          | (2_1mi_bs, (2_mi_cont|nothing))       \
;;                          | (2_1mm_bs, 2_mm_cont)                 \
;;                          | (2_1mf_bs, (2_mf_cont|nothing))       \
;;                          | (2_1b_bs, (2_b_cont|nothing))         \
;;                          | (2_1bb_bs, (2_bb_cont|nothing))       \
;;                          | (2_1mb_bs, (2_mb_cont|nothing))       \
;;                          | (2_m_cont_only, (2_m_cont|nothing))   \
;;                          | (2_b_cont_only,  (2_b_cont|nothing))  \
;;                          | (2_mi_cont_only, (2_mi_cont|nothing)) \
;;                          | (2_mm_cont_only, (2_mm_cont|nothing)) \
;;                          | (2_mf_cont_only, (2_mf_cont|nothing)) \
;;                          | (2_mb_cont_only, (2_mb_cont|nothing)) \
;;                          | (2_bb_cont_only, (2_bb_cont|nothing)) \
;;                          | (2_m_stop, (2_0mmi_cont|nothing))     \
;;                          | (2_mi_stop, (2_0mii_cont|nothing))")

;; Bypasses:

(define_bypass  1 "2_fcmp" "2_br,2_scall")
(define_bypass  0 "2_icmp" "2_br,2_scall")
(define_bypass  0 "2_tbit" "2_br,2_scall")
(define_bypass  2 "2_ld" "2_ld"  "ia64_ld_address_bypass_p")
(define_bypass  2 "2_ld" "2_st"  "ia64_st_address_bypass_p")
(define_bypass  2 "2_ld,2_ldc" "2_mmalua,2_mmmul,2_mmshf")
(define_bypass  3 "2_ilog" "2_mmalua,2_mmmul,2_mmshf")
(define_bypass  3 "2_ialu" "2_mmalua,2_mmmul,2_mmshf")
(define_bypass  3 "2_mmalua,2_mmmul,2_mmshf" "2_ialu,2_ilog,2_ishf,2_st,2_ld,2_ldc")
(define_bypass  6 "2_tofr"  "2_frfr,2_stf")
(define_bypass  7 "2_fmac"  "2_frfr,2_stf")

;; We don't use here fcmp because scall may be predicated.
(define_bypass  0 "2_fcvtfx,2_fld,2_flda,2_fldc,2_fmac,2_fmisc,2_frar_i,2_frar_m,\
                   2_frbr,2_frfr,2_frpr,2_ialu,2_ilog,2_ishf,2_ld,2_ldc,2_long_i,\
                   2_mmalua,2_mmmul,2_mmshf,2_mmshfi,2_toar_m,2_tofr,\
		   2_xmpy,2_xtd"
                  "2_scall")

(define_bypass  0 "2_unknown,2_ignore,2_stop_bit,2_br,2_fcmp,2_fcvtfx,2_fld,2_flda,2_fldc,\
                   2_fmac,2_fmisc,2_frar_i,2_frar_m,2_frbr,2_frfr,2_frpr,\
                   2_ialu,2_icmp,2_ilog,2_ishf,2_ld,2_ldc,2_chk_s_i,2_chk_s_f,2_chk_a,2_long_i,\
		   2_mmalua,2_mmmul,2_mmshf,2_mmshfi,2_nop,2_nop_b,2_nop_f,\
                   2_nop_i,2_nop_m,2_nop_x,2_rse_m,2_scall,2_sem,2_stf,2_st,\
                   2_syst_m0,2_syst_m,2_tbit,2_toar_i,2_toar_m,2_tobr,2_tofr,\
                   2_topr,2_xmpy,2_xtd,2_lfetch" "2_ignore")



;; Bundling

(define_automaton "twob")

;; Pseudo units for quicker searching for position in two packet window.  */
(define_query_cpu_unit "2_1,2_2,2_3,2_4,2_5,2_6" "twob")

;;   All possible combinations of bundles/syllables
(define_cpu_unit
   "2b_0m.ii, 2b_0m.mi, 2b_0m.fi, 2b_0m.mf, 2b_0b.bb, 2b_0m.bb,\
    2b_0m.ib, 2b_0m.mb, 2b_0m.fb, 2b_0m.lx" "twob")
(define_cpu_unit
   "2b_0mi.i, 2b_0mm.i, 2b_0mf.i, 2b_0mm.f, 2b_0bb.b, 2b_0mb.b,\
    2b_0mi.b, 2b_0mm.b, 2b_0mf.b" "twob")
(define_query_cpu_unit
   "2b_0mii., 2b_0mmi., 2b_0mfi., 2b_0mmf., 2b_0bbb., 2b_0mbb.,\
    2b_0mib., 2b_0mmb., 2b_0mfb., 2b_0mlx." "twob")

(define_cpu_unit
   "2b_1m.ii, 2b_1m.mi, 2b_1m.fi, 2b_1m.mf, 2b_1b.bb, 2b_1m.bb,\
    2b_1m.ib, 2b_1m.mb, 2b_1m.fb, 2b_1m.lx" "twob")
(define_cpu_unit
   "2b_1mi.i, 2b_1mm.i, 2b_1mf.i, 2b_1mm.f, 2b_1bb.b, 2b_1mb.b,\
    2b_1mi.b, 2b_1mm.b, 2b_1mf.b" "twob")
(define_query_cpu_unit
   "2b_1mii., 2b_1mmi., 2b_1mfi., 2b_1mmf., 2b_1bbb., 2b_1mbb.,\
    2b_1mib., 2b_1mmb., 2b_1mfb., 2b_1mlx." "twob")

;; Slot 1
(exclusion_set "2b_0m.ii"
   "2b_0m.mi, 2b_0m.fi, 2b_0m.mf, 2b_0b.bb, 2b_0m.bb,\
    2b_0m.ib, 2b_0m.mb, 2b_0m.fb, 2b_0m.lx")
(exclusion_set "2b_0m.mi"
   "2b_0m.fi, 2b_0m.mf, 2b_0b.bb, 2b_0m.bb, 2b_0m.ib,\
    2b_0m.mb, 2b_0m.fb, 2b_0m.lx")
(exclusion_set "2b_0m.fi"
   "2b_0m.mf, 2b_0b.bb, 2b_0m.bb, 2b_0m.ib, 2b_0m.mb, 2b_0m.fb, 2b_0m.lx")
(exclusion_set "2b_0m.mf"
   "2b_0b.bb, 2b_0m.bb, 2b_0m.ib, 2b_0m.mb, 2b_0m.fb, 2b_0m.lx")
(exclusion_set "2b_0b.bb" "2b_0m.bb, 2b_0m.ib, 2b_0m.mb, 2b_0m.fb, 2b_0m.lx")
(exclusion_set "2b_0m.bb" "2b_0m.ib, 2b_0m.mb, 2b_0m.fb, 2b_0m.lx")
(exclusion_set "2b_0m.ib" "2b_0m.mb, 2b_0m.fb, 2b_0m.lx")
(exclusion_set "2b_0m.mb" "2b_0m.fb, 2b_0m.lx")
(exclusion_set "2b_0m.fb" "2b_0m.lx")

;; Slot 2
(exclusion_set "2b_0mi.i"
   "2b_0mm.i, 2b_0mf.i, 2b_0mm.f, 2b_0bb.b, 2b_0mb.b,\
    2b_0mi.b, 2b_0mm.b, 2b_0mf.b, 2b_0mlx.")
(exclusion_set "2b_0mm.i"
   "2b_0mf.i, 2b_0mm.f, 2b_0bb.b, 2b_0mb.b,\
    2b_0mi.b, 2b_0mm.b, 2b_0mf.b, 2b_0mlx.")
(exclusion_set "2b_0mf.i"
   "2b_0mm.f, 2b_0bb.b, 2b_0mb.b, 2b_0mi.b, 2b_0mm.b, 2b_0mf.b, 2b_0mlx.")
(exclusion_set "2b_0mm.f"
   "2b_0bb.b, 2b_0mb.b, 2b_0mi.b, 2b_0mm.b, 2b_0mf.b, 2b_0mlx.")
(exclusion_set "2b_0bb.b" "2b_0mb.b, 2b_0mi.b, 2b_0mm.b, 2b_0mf.b, 2b_0mlx.")
(exclusion_set "2b_0mb.b" "2b_0mi.b, 2b_0mm.b, 2b_0mf.b, 2b_0mlx.")
(exclusion_set "2b_0mi.b" "2b_0mm.b, 2b_0mf.b, 2b_0mlx.")
(exclusion_set "2b_0mm.b" "2b_0mf.b, 2b_0mlx.")
(exclusion_set "2b_0mf.b" "2b_0mlx.")

;; Slot 3
(exclusion_set "2b_0mii."
   "2b_0mmi., 2b_0mfi., 2b_0mmf., 2b_0bbb., 2b_0mbb.,\
    2b_0mib., 2b_0mmb., 2b_0mfb., 2b_0mlx.")
(exclusion_set "2b_0mmi."
   "2b_0mfi., 2b_0mmf., 2b_0bbb., 2b_0mbb.,\
    2b_0mib., 2b_0mmb., 2b_0mfb., 2b_0mlx.")
(exclusion_set "2b_0mfi."
   "2b_0mmf., 2b_0bbb., 2b_0mbb., 2b_0mib., 2b_0mmb., 2b_0mfb., 2b_0mlx.")
(exclusion_set "2b_0mmf."
   "2b_0bbb., 2b_0mbb., 2b_0mib., 2b_0mmb., 2b_0mfb., 2b_0mlx.")
(exclusion_set "2b_0bbb." "2b_0mbb., 2b_0mib., 2b_0mmb., 2b_0mfb., 2b_0mlx.")
(exclusion_set "2b_0mbb." "2b_0mib., 2b_0mmb., 2b_0mfb., 2b_0mlx.")
(exclusion_set "2b_0mib." "2b_0mmb., 2b_0mfb., 2b_0mlx.")
(exclusion_set "2b_0mmb." "2b_0mfb., 2b_0mlx.")
(exclusion_set "2b_0mfb." "2b_0mlx.")

;; Slot 4
(exclusion_set "2b_1m.ii"
   "2b_1m.mi, 2b_1m.fi, 2b_1m.mf, 2b_1b.bb, 2b_1m.bb,\
    2b_1m.ib, 2b_1m.mb, 2b_1m.fb, 2b_1m.lx")
(exclusion_set "2b_1m.mi"
   "2b_1m.fi, 2b_1m.mf, 2b_1b.bb, 2b_1m.bb, 2b_1m.ib,\
    2b_1m.mb, 2b_1m.fb, 2b_1m.lx")
(exclusion_set "2b_1m.fi"
   "2b_1m.mf, 2b_1b.bb, 2b_1m.bb, 2b_1m.ib, 2b_1m.mb, 2b_1m.fb, 2b_1m.lx")
(exclusion_set "2b_1m.mf"
   "2b_1b.bb, 2b_1m.bb, 2b_1m.ib, 2b_1m.mb, 2b_1m.fb, 2b_1m.lx")
(exclusion_set "2b_1b.bb" "2b_1m.bb, 2b_1m.ib, 2b_1m.mb, 2b_1m.fb, 2b_1m.lx")
(exclusion_set "2b_1m.bb" "2b_1m.ib, 2b_1m.mb, 2b_1m.fb, 2b_1m.lx")
(exclusion_set "2b_1m.ib" "2b_1m.mb, 2b_1m.fb, 2b_1m.lx")
(exclusion_set "2b_1m.mb" "2b_1m.fb, 2b_1m.lx")
(exclusion_set "2b_1m.fb" "2b_1m.lx")

;; Slot 5
(exclusion_set "2b_1mi.i"
   "2b_1mm.i, 2b_1mf.i, 2b_1mm.f, 2b_1bb.b, 2b_1mb.b,\
    2b_1mi.b, 2b_1mm.b, 2b_1mf.b, 2b_1mlx.")
(exclusion_set "2b_1mm.i"
   "2b_1mf.i, 2b_1mm.f, 2b_1bb.b, 2b_1mb.b,\
    2b_1mi.b, 2b_1mm.b, 2b_1mf.b, 2b_1mlx.")
(exclusion_set "2b_1mf.i"
   "2b_1mm.f, 2b_1bb.b, 2b_1mb.b, 2b_1mi.b, 2b_1mm.b, 2b_1mf.b, 2b_1mlx.")
(exclusion_set "2b_1mm.f"
   "2b_1bb.b, 2b_1mb.b, 2b_1mi.b, 2b_1mm.b, 2b_1mf.b, 2b_1mlx.")
(exclusion_set "2b_1bb.b" "2b_1mb.b, 2b_1mi.b, 2b_1mm.b, 2b_1mf.b, 2b_1mlx.")
(exclusion_set "2b_1mb.b" "2b_1mi.b, 2b_1mm.b, 2b_1mf.b, 2b_1mlx.")
(exclusion_set "2b_1mi.b" "2b_1mm.b, 2b_1mf.b, 2b_1mlx.")
(exclusion_set "2b_1mm.b" "2b_1mf.b, 2b_1mlx.")
(exclusion_set "2b_1mf.b" "2b_1mlx.")

;; Slot 6
(exclusion_set "2b_1mii."
   "2b_1mmi., 2b_1mfi., 2b_1mmf., 2b_1bbb., 2b_1mbb.,\
    2b_1mib., 2b_1mmb., 2b_1mfb., 2b_1mlx.")
(exclusion_set "2b_1mmi."
   "2b_1mfi., 2b_1mmf., 2b_1bbb., 2b_1mbb.,\
    2b_1mib., 2b_1mmb., 2b_1mfb., 2b_1mlx.")
(exclusion_set "2b_1mfi."
   "2b_1mmf., 2b_1bbb., 2b_1mbb., 2b_1mib., 2b_1mmb., 2b_1mfb., 2b_1mlx.")
(exclusion_set "2b_1mmf."
   "2b_1bbb., 2b_1mbb., 2b_1mib., 2b_1mmb., 2b_1mfb., 2b_1mlx.")
(exclusion_set "2b_1bbb." "2b_1mbb., 2b_1mib., 2b_1mmb., 2b_1mfb., 2b_1mlx.")
(exclusion_set "2b_1mbb." "2b_1mib., 2b_1mmb., 2b_1mfb., 2b_1mlx.")
(exclusion_set "2b_1mib." "2b_1mmb., 2b_1mfb., 2b_1mlx.")
(exclusion_set "2b_1mmb." "2b_1mfb., 2b_1mlx.")
(exclusion_set "2b_1mfb." "2b_1mlx.")

(final_presence_set "2b_0mi.i" "2b_0m.ii")
(final_presence_set "2b_0mii." "2b_0mi.i")
(final_presence_set "2b_1mi.i" "2b_1m.ii")
(final_presence_set "2b_1mii." "2b_1mi.i")

(final_presence_set "2b_0mm.i" "2b_0m.mi")
(final_presence_set "2b_0mmi." "2b_0mm.i")
(final_presence_set "2b_1mm.i" "2b_1m.mi")
(final_presence_set "2b_1mmi." "2b_1mm.i")

(final_presence_set "2b_0mf.i" "2b_0m.fi")
(final_presence_set "2b_0mfi." "2b_0mf.i")
(final_presence_set "2b_1mf.i" "2b_1m.fi")
(final_presence_set "2b_1mfi." "2b_1mf.i")

(final_presence_set "2b_0mm.f" "2b_0m.mf")
(final_presence_set "2b_0mmf." "2b_0mm.f")
(final_presence_set "2b_1mm.f" "2b_1m.mf")
(final_presence_set "2b_1mmf." "2b_1mm.f")

(final_presence_set "2b_0bb.b" "2b_0b.bb")
(final_presence_set "2b_0bbb." "2b_0bb.b")
(final_presence_set "2b_1bb.b" "2b_1b.bb")
(final_presence_set "2b_1bbb." "2b_1bb.b")

(final_presence_set "2b_0mb.b" "2b_0m.bb")
(final_presence_set "2b_0mbb." "2b_0mb.b")
(final_presence_set "2b_1mb.b" "2b_1m.bb")
(final_presence_set "2b_1mbb." "2b_1mb.b")

(final_presence_set "2b_0mi.b" "2b_0m.ib")
(final_presence_set "2b_0mib." "2b_0mi.b")
(final_presence_set "2b_1mi.b" "2b_1m.ib")
(final_presence_set "2b_1mib." "2b_1mi.b")

(final_presence_set "2b_0mm.b" "2b_0m.mb")
(final_presence_set "2b_0mmb." "2b_0mm.b")
(final_presence_set "2b_1mm.b" "2b_1m.mb")
(final_presence_set "2b_1mmb." "2b_1mm.b")

(final_presence_set "2b_0mf.b" "2b_0m.fb")
(final_presence_set "2b_0mfb." "2b_0mf.b")
(final_presence_set "2b_1mf.b" "2b_1m.fb")
(final_presence_set "2b_1mfb." "2b_1mf.b")

(final_presence_set "2b_0mlx." "2b_0m.lx")
(final_presence_set "2b_1mlx." "2b_1m.lx")

;;  See the corresponding comment in non-bundling section above.
(final_presence_set
   "2b_1m.lx"
   "2b_0mmi.,2b_0mfi.,2b_0mmf.,2b_0mib.,2b_0mmb.,2b_0mfb.,2b_0mlx.")
(final_presence_set "2b_1b.bb" "2b_0mii.,2b_0mmi.,2b_0mfi.,2b_0mmf.,2b_0mlx.")
(final_presence_set
   "2b_1m.ii,2b_1m.mi,2b_1m.fi,2b_1m.mf,2b_1m.bb,2b_1m.ib,2b_1m.mb,2b_1m.fb"
   "2b_0mii.,2b_0mmi.,2b_0mfi.,2b_0mmf.,2b_0mib.,2b_0mmb.,2b_0mfb.,2b_0mlx.")

;;  Ports/units (nb means nop.b insn issued into given port):
(define_cpu_unit
   "2b_um0, 2b_um1, 2b_um2, 2b_um3, 2b_ui0, 2b_ui1, 2b_uf0, 2b_uf1,\
    2b_ub0, 2b_ub1, 2b_ub2, 2b_unb0, 2b_unb1, 2b_unb2" "twob")

(exclusion_set "2b_ub0" "2b_unb0")
(exclusion_set "2b_ub1" "2b_unb1")
(exclusion_set "2b_ub2" "2b_unb2")

;; The following rules are used to decrease number of alternatives.
;; They are consequences of Itanium2 microarchitecture.  They also
;; describe the following rules mentioned in Itanium2
;; microarchitecture: rules mentioned in Itanium2 microarchitecture:
;; o "BBB/MBB: Always splits issue after either of these bundles".
;; o "MIB BBB: Split issue after the first bundle in this pair".
(exclusion_set
   "2b_0b.bb,2b_0bb.b,2b_0bbb.,2b_0m.bb,2b_0mb.b,2b_0mbb."
   "2b_1m.ii,2b_1m.mi,2b_1m.fi,2b_1m.mf,2b_1b.bb,2b_1m.bb,\
    2b_1m.ib,2b_1m.mb,2b_1m.fb,2b_1m.lx")
(exclusion_set "2b_0m.ib,2b_0mi.b,2b_0mib." "2b_1b.bb")

;;; "MIB/MFB/MMB: Splits issue after any of these bundles unless the
;;; B-slot contains a nop.b or a brp instruction".
;;;   "The B in an MIB/MFB/MMB bundle disperses to B0 if it is a brp or
;;; nop.b, otherwise it disperses to B2".
(final_absence_set
   "2b_1m.ii, 2b_1m.mi, 2b_1m.fi, 2b_1m.mf, 2b_1b.bb, 2b_1m.bb,\
    2b_1m.ib, 2b_1m.mb, 2b_1m.fb, 2b_1m.lx"
   "2b_0mib. 2b_ub2, 2b_0mfb. 2b_ub2, 2b_0mmb. 2b_ub2")

;; This is necessary to start new processor cycle when we meet stop bit.
(define_cpu_unit "2b_stop" "twob")
(final_absence_set
   "2b_0m.ii,2b_0mi.i,2b_0mii.,2b_0m.mi,2b_0mm.i,2b_0mmi.,\
    2b_0m.fi,2b_0mf.i,2b_0mfi.,\
    2b_0m.mf,2b_0mm.f,2b_0mmf.,2b_0b.bb,2b_0bb.b,2b_0bbb.,\
    2b_0m.bb,2b_0mb.b,2b_0mbb.,\
    2b_0m.ib,2b_0mi.b,2b_0mib.,2b_0m.mb,2b_0mm.b,2b_0mmb.,\
    2b_0m.fb,2b_0mf.b,2b_0mfb.,2b_0m.lx,2b_0mlx., \
    2b_1m.ii,2b_1mi.i,2b_1mii.,2b_1m.mi,2b_1mm.i,2b_1mmi.,\
    2b_1m.fi,2b_1mf.i,2b_1mfi.,\
    2b_1m.mf,2b_1mm.f,2b_1mmf.,2b_1b.bb,2b_1bb.b,2b_1bbb.,\
    2b_1m.bb,2b_1mb.b,2b_1mbb.,\
    2b_1m.ib,2b_1mi.b,2b_1mib.,2b_1m.mb,2b_1mm.b,2b_1mmb.,\
    2b_1m.fb,2b_1mf.b,2b_1mfb.,2b_1m.lx,2b_1mlx."
   "2b_stop")

;;   The issue logic can reorder M slot insns between different subtypes
;; but cannot reorder insn within the same subtypes.  The following
;; constraint is enough to describe this.
(final_presence_set "2b_um1" "2b_um0")
(final_presence_set "2b_um3" "2b_um2")

;;   The insn in the 1st I slot of the two bundle issue group will issue
;; to I0.  The second I slot insn will issue to I1.
(final_presence_set "2b_ui1" "2b_ui0")

;;  For exceptions of I insns:
(define_cpu_unit "2b_only_ui0" "twob")
(final_absence_set "2b_only_ui0"  "2b_ui1")

;; Insns

(define_reservation "2b_M"
  "((2b_0m.ii|2b_0m.mi|2b_0m.fi|2b_0m.mf|2b_0m.bb\
     |2b_0m.ib|2b_0m.mb|2b_0m.fb|2b_0m.lx)+2_1\
    |(2b_1m.ii|2b_1m.mi|2b_1m.fi|2b_1m.mf|2b_1m.bb\
      |2b_1m.ib|2b_1m.mb|2b_1m.fb|2b_1m.lx)+2_4\
    |(2b_0mm.i|2b_0mm.f|2b_0mm.b)+2_2\
    |(2b_1mm.i|2b_1mm.f|2b_1mm.b)+2_5)\
   +(2b_um0|2b_um1|2b_um2|2b_um3)")

(define_reservation "2b_M_only_um0"
  "((2b_0m.ii|2b_0m.mi|2b_0m.fi|2b_0m.mf|2b_0m.bb\
     |2b_0m.ib|2b_0m.mb|2b_0m.fb|2b_0m.lx)+2_1\
    |(2b_1m.ii|2b_1m.mi|2b_1m.fi|2b_1m.mf|2b_1m.bb\
      |2b_1m.ib|2b_1m.mb|2b_1m.fb|2b_1m.lx)+2_4\
    |(2b_0mm.i|2b_0mm.f|2b_0mm.b)+2_2\
    |(2b_1mm.i|2b_1mm.f|2b_1mm.b)+2_5)\
   +2b_um0")

(define_reservation "2b_M_only_um2"
  "((2b_0m.ii|2b_0m.mi|2b_0m.fi|2b_0m.mf|2b_0m.bb\
     |2b_0m.ib|2b_0m.mb|2b_0m.fb|2b_0m.lx)+2_1\
    |(2b_1m.ii|2b_1m.mi|2b_1m.fi|2b_1m.mf|2b_1m.bb\
      |2b_1m.ib|2b_1m.mb|2b_1m.fb|2b_1m.lx)+2_4\
    |(2b_0mm.i|2b_0mm.f|2b_0mm.b)+2_2\
    |(2b_1mm.i|2b_1mm.f|2b_1mm.b)+2_5)\
   +2b_um2")

(define_reservation "2b_M_only_um01"
  "((2b_0m.ii|2b_0m.mi|2b_0m.fi|2b_0m.mf|2b_0m.bb\
     |2b_0m.ib|2b_0m.mb|2b_0m.fb|2b_0m.lx)+2_1\
    |(2b_1m.ii|2b_1m.mi|2b_1m.fi|2b_1m.mf|2b_1m.bb\
      |2b_1m.ib|2b_1m.mb|2b_1m.fb|2b_1m.lx)+2_4\
    |(2b_0mm.i|2b_0mm.f|2b_0mm.b)+2_2\
    |(2b_1mm.i|2b_1mm.f|2b_1mm.b)+2_5)\
   +(2b_um0|2b_um1)")

(define_reservation "2b_M_only_um23"
  "((2b_0m.ii|2b_0m.mi|2b_0m.fi|2b_0m.mf|2b_0m.bb\
     |2b_0m.ib|2b_0m.mb|2b_0m.fb|2b_0m.lx)+2_1\
    |(2b_1m.ii|2b_1m.mi|2b_1m.fi|2b_1m.mf|2b_1m.bb\
      |2b_1m.ib|2b_1m.mb|2b_1m.fb|2b_1m.lx)+2_4\
    |(2b_0mm.i|2b_0mm.f|2b_0mm.b)+2_2\
    |(2b_1mm.i|2b_1mm.f|2b_1mm.b)+2_5)\
   +(2b_um2|2b_um3)")

;; I instruction is dispersed to the lowest numbered I unit
;; not already in use.  Remember about possible splitting.
(define_reservation "2b_I"
  "2b_0mi.i+2_2+2b_ui0|2b_0mii.+2_3+(2b_ui0|2b_ui1)|2b_0mmi.+2_3+2b_ui0\
   |2b_0mfi.+2_3+2b_ui0|2b_0mi.b+2_2+2b_ui0\
   |(2b_1mi.i+2_5|2b_1mi.b+2_5)+(2b_ui0|2b_ui1)\
   |(2b_1mii.|2b_1mmi.|2b_1mfi.)+2_6+(2b_ui0|2b_ui1)")

;; "An F slot in the 1st bundle disperses to F0".
;; "An F slot in the 2st bundle disperses to F1".
(define_reservation "2b_F"
   "2b_0mf.i+2_2+2b_uf0|2b_0mmf.+2_3+2b_uf0|2b_0mf.b+2_2+2b_uf0\
    |2b_1mf.i+2_5+2b_uf1|2b_1mmf.+2_6+2b_uf1|2b_1mf.b+2_5+2b_uf1")

;;; "Each B slot in MBB or BBB bundle disperses to the corresponding B
;;; unit. That is, a B slot in 1st position is dispersed to B0.  In the
;;; 2nd position it is dispersed to B2".
(define_reservation "2b_NB"
    "2b_0b.bb+2_1+2b_unb0|2b_0bb.b+2_2+2b_unb1|2b_0bbb.+2_3+2b_unb2\
     |2b_0mb.b+2_2+2b_unb1|2b_0mbb.+2_3+2b_unb2\
     |2b_0mib.+2_3+2b_unb0|2b_0mmb.+2_3+2b_unb0|2b_0mfb.+2_3+2b_unb0\
     |2b_1b.bb+2_4+2b_unb0|2b_1bb.b+2_5+2b_unb1\
     |2b_1bbb.+2_6+2b_unb2|2b_1mb.b+2_5+2b_unb1|2b_1mbb.+2_6+2b_unb2\
     |2b_1mib.+2_6+2b_unb0|2b_1mmb.+2_6+2b_unb0|2b_1mfb.+2_6+2b_unb0")

(define_reservation "2b_B"
   "2b_0b.bb+2_1+2b_ub0|2b_0bb.b+2_2+2b_ub1|2b_0bbb.+2_3+2b_ub2\
    |2b_0mb.b+2_2+2b_ub1|2b_0mbb.+2_3+2b_ub2|2b_0mib.+2_3+2b_ub2\
    |2b_0mfb.+2_3+2b_ub2|2b_1b.bb+2_4+2b_ub0|2b_1bb.b+2_5+2b_ub1\
    |2b_1bbb.+2_6+2b_ub2|2b_1mb.b+2_5+2b_ub1\
    |2b_1mib.+2_6+2b_ub2|2b_1mmb.+2_6+2b_ub2|2b_1mfb.+2_6+2b_ub2")

;;   For the MLI template, the I slot insn is always assigned to port I0
;; if it is in the first bundle or it is assigned to port I1 if it is in
;; the second bundle.
(define_reservation "2b_L"
                    "2b_0mlx.+2_3+2b_ui0+2b_uf0|2b_1mlx.+2_6+2b_ui1+2b_uf1")

;;   Should we describe that A insn in I slot can be issued into M
;; ports?  I think it is not necessary because of multipass
;; scheduling.  For example, the multipass scheduling could use
;; MMI-MMI instead of MII-MII where the two last I slots contain A
;; insns (even if the case is complicated by use-def conflicts).
;;
;; In any case we could describe it as
;;    (define_cpu_unit "2b_ui1_0pres,2b_ui1_1pres,2b_ui1_2pres,2b_ui1_3pres"
;;                     "twob")
;;    (final_presence_set "2b_ui1_0pres,2b_ui1_1pres,2b_ui1_2pres,2b_ui1_3pres"
;;                        "2b_ui1")
;;    (define_reservation "b_A"
;;       "b_M|b_I\
;;        |(2b_1mi.i+2_5|2b_1mii.+2_6|2b_1mmi.+2_6|2b_1mfi.+2_6|2b_1mi.b+2_5)\
;;         +(2b_um0|2b_um1|2b_um2|2b_um3)\
;;         +(2b_ui1_0pres|2b_ui1_1pres|2b_ui1_2pres|2b_ui1_3pres)")

(define_reservation "2b_A" "2b_M|2b_I")

;; We assume that there is no insn issued on the same cycle as the
;; unknown insn.
(define_cpu_unit "2b_empty" "twob")
(exclusion_set "2b_empty"
    "2b_0m.ii,2b_0m.mi,2b_0m.fi,2b_0m.mf,2b_0b.bb,2b_0m.bb,\
     2b_0m.ib,2b_0m.mb,2b_0m.fb,2b_0m.lx,2b_0mm.i")

(define_cpu_unit
   "2b_0m_bs, 2b_0mi_bs, 2b_0mm_bs, 2b_0mf_bs, 2b_0b_bs, 2b_0bb_bs, 2b_0mb_bs"
   "twob")
(define_cpu_unit
   "2b_1m_bs, 2b_1mi_bs, 2b_1mm_bs, 2b_1mf_bs, 2b_1b_bs, 2b_1bb_bs, 2b_1mb_bs"
   "twob")

(define_cpu_unit "2b_m_cont, 2b_mi_cont, 2b_mm_cont, 2b_mf_cont, 2b_mb_cont,\
	          2b_b_cont, 2b_bb_cont" "twob")

;; For stop in the middle of the bundles.
(define_cpu_unit "2b_m_stop, 2b_m0_stop, 2b_m1_stop, 2b_0mmi_cont" "twob")
(define_cpu_unit "2b_mi_stop, 2b_mi0_stop, 2b_mi1_stop, 2b_0mii_cont" "twob")

(final_presence_set "2b_0m_bs"
   "2b_0m.ii, 2b_0m.mi, 2b_0m.mf, 2b_0m.fi, 2b_0m.bb,\
    2b_0m.ib, 2b_0m.fb, 2b_0m.mb, 2b_0m.lx")
(final_presence_set "2b_1m_bs"
   "2b_1m.ii, 2b_1m.mi, 2b_1m.mf, 2b_1m.fi, 2b_1m.bb,\
    2b_1m.ib, 2b_1m.fb, 2b_1m.mb, 2b_1m.lx")
(final_presence_set "2b_0mi_bs"  "2b_0mi.i, 2b_0mi.i")
(final_presence_set "2b_1mi_bs"  "2b_1mi.i, 2b_1mi.i")
(final_presence_set "2b_0mm_bs"  "2b_0mm.i, 2b_0mm.f, 2b_0mm.b")
(final_presence_set "2b_1mm_bs"  "2b_1mm.i, 2b_1mm.f, 2b_1mm.b")
(final_presence_set "2b_0mf_bs"  "2b_0mf.i, 2b_0mf.b")
(final_presence_set "2b_1mf_bs"  "2b_1mf.i, 2b_1mf.b")
(final_presence_set "2b_0b_bs"  "2b_0b.bb")
(final_presence_set "2b_1b_bs"  "2b_1b.bb")
(final_presence_set "2b_0bb_bs"  "2b_0bb.b")
(final_presence_set "2b_1bb_bs"  "2b_1bb.b")
(final_presence_set "2b_0mb_bs"  "2b_0mb.b")
(final_presence_set "2b_1mb_bs"  "2b_1mb.b")

(exclusion_set "2b_0m_bs"
   "2b_0mi.i, 2b_0mm.i, 2b_0mm.f, 2b_0mf.i, 2b_0mb.b,\
    2b_0mi.b, 2b_0mf.b, 2b_0mm.b, 2b_0mlx., 2b_m0_stop")
(exclusion_set "2b_1m_bs"
   "2b_1mi.i, 2b_1mm.i, 2b_1mm.f, 2b_1mf.i, 2b_1mb.b,\
    2b_1mi.b, 2b_1mf.b, 2b_1mm.b, 2b_1mlx., 2b_m1_stop")
(exclusion_set "2b_0mi_bs"  "2b_0mii., 2b_0mib., 2b_mi0_stop")
(exclusion_set "2b_1mi_bs"  "2b_1mii., 2b_1mib., 2b_mi1_stop")
(exclusion_set "2b_0mm_bs"  "2b_0mmi., 2b_0mmf., 2b_0mmb.")
(exclusion_set "2b_1mm_bs"  "2b_1mmi., 2b_1mmf., 2b_1mmb.")
(exclusion_set "2b_0mf_bs"  "2b_0mfi., 2b_0mfb.")
(exclusion_set "2b_1mf_bs"  "2b_1mfi., 2b_1mfb.")
(exclusion_set "2b_0b_bs"  "2b_0bb.b")
(exclusion_set "2b_1b_bs"  "2b_1bb.b")
(exclusion_set "2b_0bb_bs"  "2b_0bbb.")
(exclusion_set "2b_1bb_bs"  "2b_1bbb.")
(exclusion_set "2b_0mb_bs"  "2b_0mbb.")
(exclusion_set "2b_1mb_bs"  "2b_1mbb.")

(exclusion_set
   "2b_0m_bs, 2b_0mi_bs, 2b_0mm_bs, 2b_0mf_bs, 2b_0b_bs, 2b_0bb_bs, 2b_0mb_bs,
    2b_1m_bs, 2b_1mi_bs, 2b_1mm_bs, 2b_1mf_bs, 2b_1b_bs, 2b_1bb_bs, 2b_1mb_bs"
   "2b_stop")

(final_presence_set
   "2b_0mi.i, 2b_0mm.i, 2b_0mf.i, 2b_0mm.f, 2b_0mb.b,\
    2b_0mi.b, 2b_0mm.b, 2b_0mf.b, 2b_0mlx."
   "2b_m_cont")
(final_presence_set "2b_0mii., 2b_0mib." "2b_mi_cont")
(final_presence_set "2b_0mmi., 2b_0mmf., 2b_0mmb." "2b_mm_cont")
(final_presence_set "2b_0mfi., 2b_0mfb." "2b_mf_cont")
(final_presence_set "2b_0bb.b" "2b_b_cont")
(final_presence_set "2b_0bbb." "2b_bb_cont")
(final_presence_set "2b_0mbb." "2b_mb_cont")

(exclusion_set
   "2b_0m.ii, 2b_0m.mi, 2b_0m.fi, 2b_0m.mf, 2b_0b.bb, 2b_0m.bb,\
    2b_0m.ib, 2b_0m.mb, 2b_0m.fb, 2b_0m.lx"
   "2b_m_cont, 2b_mi_cont, 2b_mm_cont, 2b_mf_cont,\
    2b_mb_cont, 2b_b_cont, 2b_bb_cont")

(exclusion_set "2b_empty"
               "2b_m_cont,2b_mi_cont,2b_mm_cont,2b_mf_cont,\
                2b_mb_cont,2b_b_cont,2b_bb_cont")

;; For m;mi bundle
(final_presence_set "2b_m0_stop" "2b_0m.mi")
(final_presence_set "2b_0mm.i" "2b_0mmi_cont")
(exclusion_set "2b_0mmi_cont"
   "2b_0m.ii, 2b_0m.mi, 2b_0m.fi, 2b_0m.mf, 2b_0b.bb, 2b_0m.bb,\
    2b_0m.ib, 2b_0m.mb, 2b_0m.fb, 2b_0m.lx")
(exclusion_set "2b_m0_stop" "2b_0mm.i")
(final_presence_set "2b_m1_stop" "2b_1m.mi")
(exclusion_set "2b_m1_stop" "2b_1mm.i")
(final_presence_set "2b_m_stop" "2b_m0_stop, 2b_m1_stop")

;; For mi;i bundle
(final_presence_set "2b_mi0_stop" "2b_0mi.i")
(final_presence_set "2b_0mii." "2b_0mii_cont")
(exclusion_set "2b_0mii_cont"
   "2b_0m.ii, 2b_0m.mi, 2b_0m.fi, 2b_0m.mf, 2b_0b.bb, 2b_0m.bb,\
    2b_0m.ib, 2b_0m.mb, 2b_0m.fb, 2b_0m.lx")
(exclusion_set "2b_mi0_stop" "2b_0mii.")
(final_presence_set "2b_mi1_stop" "2b_1mi.i")
(exclusion_set "2b_mi1_stop" "2b_1mii.")
(final_presence_set "2b_mi_stop" "2b_mi0_stop, 2b_mi1_stop")

(final_absence_set
   "2b_0m.ii,2b_0mi.i,2b_0mii.,2b_0m.mi,2b_0mm.i,2b_0mmi.,\
    2b_0m.fi,2b_0mf.i,2b_0mfi.,2b_0m.mf,2b_0mm.f,2b_0mmf.,\
    2b_0b.bb,2b_0bb.b,2b_0bbb.,2b_0m.bb,2b_0mb.b,2b_0mbb.,\
    2b_0m.ib,2b_0mi.b,2b_0mib.,2b_0m.mb,2b_0mm.b,2b_0mmb.,\
    2b_0m.fb,2b_0mf.b,2b_0mfb.,2b_0m.lx,2b_0mlx., \
    2b_1m.ii,2b_1mi.i,2b_1mii.,2b_1m.mi,2b_1mm.i,2b_1mmi.,\
    2b_1m.fi,2b_1mf.i,2b_1mfi.,2b_1m.mf,2b_1mm.f,2b_1mmf.,\
    2b_1b.bb,2b_1bb.b,2b_1bbb.,2b_1m.bb,2b_1mb.b,2b_1mbb.,\
    2b_1m.ib,2b_1mi.b,2b_1mib.,2b_1m.mb,2b_1mm.b,2b_1mmb.,\
    2b_1m.fb,2b_1mf.b,2b_1mfb.,2b_1m.lx,2b_1mlx."
   "2b_m0_stop,2b_m1_stop,2b_mi0_stop,2b_mi1_stop")

(define_insn_reservation "2b_stop_bit" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "stop_bit"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_stop|2b_m0_stop|2b_m1_stop|2b_mi0_stop|2b_mi1_stop")
(define_insn_reservation "2b_br"      0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "br"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_B")
(define_insn_reservation "2b_scall"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "scall"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_B")
(define_insn_reservation "2b_fcmp"    2
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "fcmp"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_F")
(define_insn_reservation "2b_fcvtfx"  4
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "fcvtfx"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_F")
(define_insn_reservation "2b_fld"     6
  (and (and (and (and (eq_attr "cpu" "itanium2")
                      (eq_attr "itanium_class" "fld"))
                 (eq_attr "data_speculative" "no"))
            (eq_attr "check_load" "no"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M")
(define_insn_reservation "2b_flda"    6
  (and (and (and (eq_attr "cpu" "itanium2")
                 (eq_attr "itanium_class" "fld"))
            (eq_attr "data_speculative" "yes"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um01")
(define_insn_reservation "2b_fldc"    0
  (and (and (and (eq_attr "cpu" "itanium2")
                 (eq_attr "itanium_class" "fld"))
            (eq_attr "check_load" "yes"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um01")

(define_insn_reservation "2b_fldp"    6
  (and (and (and (eq_attr "cpu" "itanium2")
		 (eq_attr "itanium_class" "fldp"))
	    (eq_attr "check_load" "no"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um01")
(define_insn_reservation "2b_fldpc"   0
  (and (and (and (eq_attr "cpu" "itanium2")
		 (eq_attr "itanium_class" "fldp"))
	    (eq_attr "check_load" "yes"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um01")

(define_insn_reservation "2b_fmac"    4
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "fmac"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_F")
(define_insn_reservation "2b_fmisc"   4
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "fmisc"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_F")

;; Latency time ???
(define_insn_reservation "2b_frar_i" 13
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "frar_i"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_I+2b_only_ui0")
;; Latency time ???
(define_insn_reservation "2b_frar_m"  6
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "frar_m"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um2")
(define_insn_reservation "2b_frbr"    2
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "frbr"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_I+2b_only_ui0")
(define_insn_reservation "2b_frfr"    5				  
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "frfr"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um2")
(define_insn_reservation "2b_frpr"    2				  
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "frpr"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_I+2b_only_ui0")

(define_insn_reservation "2b_ialu"      1
    (and (and (eq_attr "cpu" "itanium2")
              (eq_attr "itanium_class" "ialu"))
         (ne (symbol_ref "bundling_p") (const_int 0)))
    "2b_A")
(define_insn_reservation "2b_icmp"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "icmp"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_A")
(define_insn_reservation "2b_ilog"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "ilog"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_A")
(define_insn_reservation "2b_mmalua"  2
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "mmalua"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_A")
;; Latency time ???
(define_insn_reservation "2b_ishf"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "ishf"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_I+2b_only_ui0")

(define_insn_reservation "2b_ld"      1
  (and (and (and (eq_attr "cpu" "itanium2")
                 (eq_attr "itanium_class" "ld"))
            (eq_attr "check_load" "no"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um01")
(define_insn_reservation "2b_ldc"     0
  (and (and (and (eq_attr "cpu" "itanium2")
                 (eq_attr "itanium_class" "ld"))
            (eq_attr "check_load" "yes"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um01")

(define_insn_reservation "2b_long_i"  1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "long_i"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_L")

;; Latency time ???
(define_insn_reservation "2b_mmmul"   2
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "mmmul"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_I+2b_only_ui0")
;; Latency time ???
(define_insn_reservation "2b_mmshf"   2
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "mmshf"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_I")
;; Latency time ???
(define_insn_reservation "2b_mmshfi"  1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "mmshfi"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_I")

(define_insn_reservation "2b_rse_m"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "rse_m"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
   "(2b_0m.ii|2b_0m.mi|2b_0m.fi|2b_0m.mf|2b_0m.bb\
     |2b_0m.ib|2b_0m.mb|2b_0m.fb|2b_0m.lx)+2_1+2b_um0")
(define_insn_reservation "2b_sem"     0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "sem"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um23")

(define_insn_reservation "2b_stf"     1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "stf"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um23")
(define_insn_reservation "2b_st"      1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "st"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um23")
(define_insn_reservation "2b_syst_m0" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "syst_m0"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um2")
(define_insn_reservation "2b_syst_m"  0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "syst_m"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um0")
;; Reservation???
(define_insn_reservation "2b_tbit"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "tbit"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_I+2b_only_ui0")
(define_insn_reservation "2b_toar_i"  0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "toar_i"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_I+2b_only_ui0")
;; Latency time ???
(define_insn_reservation "2b_toar_m"  5
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "toar_m"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um2")
;; Latency time ???
(define_insn_reservation "2b_tobr"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "tobr"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_I+2b_only_ui0")
(define_insn_reservation "2b_tofr"    5
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "tofr"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um23")
;; Latency time ???
(define_insn_reservation "2b_topr"    1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "topr"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_I+2b_only_ui0")

(define_insn_reservation "2b_xmpy"    4
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "xmpy"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_F")
;; Latency time ???
(define_insn_reservation "2b_xtd"     1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "xtd"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_I")

(define_insn_reservation "2b_chk_s_i" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "chk_s_i"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_I|2b_M_only_um23")
(define_insn_reservation "2b_chk_s_f" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "chk_s_f"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um23")
(define_insn_reservation "2b_chk_a"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "chk_a"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um01")

(define_insn_reservation "2b_lfetch"  0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "lfetch"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M_only_um01")
(define_insn_reservation "2b_nop_m"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop_m"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_M")
(define_insn_reservation "2b_nop_b"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop_b"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_NB")
(define_insn_reservation "2b_nop_i"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop_i"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_I")
(define_insn_reservation "2b_nop_f"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop_f"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_F")
(define_insn_reservation "2b_nop_x"   0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop_x"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_L")
(define_insn_reservation "2b_unknown" 1
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "unknown"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "2b_empty")
(define_insn_reservation "2b_nop" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "nop"))
       (ne (symbol_ref "bundling_p") (const_int 0)))
  "2b_M|2b_NB|2b_I|2b_F")
(define_insn_reservation "2b_ignore" 0
  (and (and (eq_attr "cpu" "itanium2")
            (eq_attr "itanium_class" "ignore"))
       (ne (symbol_ref "bundling_p") (const_int 0))) "nothing")

(define_insn_reservation "2b_pre_cycle" 0
   (and (and (eq_attr "cpu" "itanium2")
             (eq_attr "itanium_class" "pre_cycle"))
        (ne (symbol_ref "bundling_p") (const_int 0)))
                         "(2b_0m_bs, 2b_m_cont)     \
                          | (2b_0mi_bs, 2b_mi_cont) \
                          | (2b_0mm_bs, 2b_mm_cont) \
                          | (2b_0mf_bs, 2b_mf_cont) \
                          | (2b_0b_bs, 2b_b_cont)   \
                          | (2b_0bb_bs, 2b_bb_cont) \
                          | (2b_0mb_bs, 2b_mb_cont) \
                          | (2b_1m_bs, 2b_m_cont)   \
                          | (2b_1mi_bs, 2b_mi_cont) \
                          | (2b_1mm_bs, 2b_mm_cont) \
                          | (2b_1mf_bs, 2b_mf_cont) \
                          | (2b_1b_bs, 2b_b_cont)   \
                          | (2b_1bb_bs, 2b_bb_cont) \
                          | (2b_1mb_bs, 2b_mb_cont) \
                          | (2b_m_stop, 2b_0mmi_cont)   \
                          | (2b_mi_stop, 2b_0mii_cont)")

