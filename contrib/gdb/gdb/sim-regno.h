/* Generic remote debugging interface for simulators.

   Copyright 2002 Free Software Foundation, Inc.

   Contributed by Red Hat, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef SIM_REGNO_H
#define SIM_REGNO_H

/* The REGISTER_SIM_REGNO(REGNUM) method, when there is a
   corresponding simulator register, returns that register number as a
   cardinal.  When there is no corresponding register, it returns a
   negative value.  */

enum sim_regno {
  /* Normal sane architecture.  The simulator is known to not model
     this register.  */
  SIM_REGNO_DOES_NOT_EXIST = -1,
  /* For possible backward compatibility.  The register cache doesn't
     have a corresponding name.  Skip the register entirely.  */
  LEGACY_SIM_REGNO_IGNORE = -2
};

/* Treat all raw registers as valid.  */

extern int one2one_register_sim_regno (int regnum);

#endif
