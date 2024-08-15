#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+

# This is simply to aide in creating the entries in the order of the value of
# the device-global NI signal/terminal constants defined in comedi.h
import comedi_h
import os, sys, re
from csv_collection import CSVCollection


def c_to_o(filename, prefix='\t\t\t\t\t   ni_routing/', suffix=' \\'):
  if not filename.endswith('.c'):
    return ''
  return prefix + filename.rpartition('.c')[0] + '.o' + suffix


def routedict_to_structinit_single(name, D, return_name=False):
  Locals = dict()
  lines = [
    '\t.family = "{}",'.format(name),
    '\t.register_values = {',
    '\t\t/*',
    '\t\t * destination = {',
	  '\t\t *              source          = register value,',
	  '\t\t *              ...',
	  '\t\t * }',
		'\t\t */',
  ]
  if (False):
    # print table with index0:src, index1:dest
    D0 = D # (src-> dest->reg_value)
    #D1 : destD
  else:
    D0 = dict()
    for src, destD in D.items():
      for dest, val in destD.items():
        D0.setdefault(dest, {})[src] = val


  D0 = sorted(D0.items(), key=lambda i: eval(i[0], comedi_h.__dict__, Locals))

  for D0_sig, D1_D in D0:
    D1 = sorted(D1_D.items(), key=lambda i: eval(i[0], comedi_h.__dict__, Locals))

    lines.append('\t\t[B({})] = {{'.format(D0_sig))
    for D1_sig, value in D1:
      if not re.match('[VIU]\([^)]*\)', value):
        sys.stderr.write('Invalid register format: {}\n'.format(repr(value)))
        sys.stderr.write(
          'Register values should be formatted with V(),I(),or U()\n')
        raise RuntimeError('Invalid register values format')
      lines.append('\t\t\t[B({})]\t= {},'.format(D1_sig, value))
    lines.append('\t\t},')
  lines.append('\t},')

  lines = '\n'.join(lines)
  if return_name:
    return N, lines
  else:
    return lines


def routedict_to_routelist_single(name, D, indent=1):
  Locals = dict()

  indents = dict(
    I0 = '\t'*(indent),
    I1 = '\t'*(indent+1),
    I2 = '\t'*(indent+2),
    I3 = '\t'*(indent+3),
    I4 = '\t'*(indent+4),
  )

  if (False):
    # data is src -> dest-list
    D0 = D
    keyname = 'src'
    valname = 'dest'
  else:
    # data is dest -> src-list
    keyname = 'dest'
    valname = 'src'
    D0 = dict()
    for src, destD in D.items():
      for dest, val in destD.items():
        D0.setdefault(dest, {})[src] = val

  # Sort by order of device-global names (numerically)
  D0 = sorted(D0.items(), key=lambda i: eval(i[0], comedi_h.__dict__, Locals))

  lines = [ '{I0}.device = "{name}",\n'
            '{I0}.routes = (struct ni_route_set[]){{'
            .format(name=name, **indents) ]
  for D0_sig, D1_D in D0:
    D1 = [ k for k,v in D1_D.items() if v ]
    D1.sort(key=lambda i: eval(i, comedi_h.__dict__, Locals))

    lines.append('{I1}{{\n{I2}.{keyname} = {D0_sig},\n'
                         '{I2}.{valname} = (int[]){{'
                 .format(keyname=keyname, valname=valname, D0_sig=D0_sig, **indents)
    )
    for D1_sig in D1:
      lines.append( '{I3}{D1_sig},'.format(D1_sig=D1_sig, **indents) )
    lines.append( '{I3}0, /* Termination */'.format(**indents) )

    lines.append('{I2}}}\n{I1}}},'.format(**indents))

  lines.append('{I1}{{ /* Termination of list */\n{I2}.{keyname} = 0,\n{I1}}},'
               .format(keyname=keyname, **indents))

  lines.append('{I0}}},'.format(**indents))

  return '\n'.join(lines)


class DeviceRoutes(CSVCollection):
  MKFILE_SEGMENTS = 'device-route.mk'
  SET_C = 'ni_device_routes.c'
  ITEMS_DIR = 'ni_device_routes'
  EXTERN_H = 'all.h'
  OUTPUT_DIR = 'c'

  output_file_top = """\
// SPDX-License-Identifier: GPL-2.0+
/*
 *  comedi/drivers/ni_routing/{filename}
 *  List of valid routes for specific NI boards.
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2016 Spencer E. Olson <olsonse@umich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 * The contents of this file are generated using the tools in
 * comedi/drivers/ni_routing/tools
 *
 * Please use those tools to help maintain the contents of this file.
 */

#include "ni_device_routes.h"
#include "{extern_h}"\
""".format(filename=SET_C, extern_h=os.path.join(ITEMS_DIR, EXTERN_H))

  extern_header = """\
/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  comedi/drivers/ni_routing/{filename}
 *  List of valid routes for specific NI boards.
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2016 Spencer E. Olson <olsonse@umich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 * The contents of this file are generated using the tools in
 * comedi/drivers/ni_routing/tools
 *
 * Please use those tools to help maintain the contents of this file.
 */

#ifndef _COMEDI_DRIVERS_NI_ROUTING_NI_DEVICE_ROUTES_EXTERN_H
#define _COMEDI_DRIVERS_NI_ROUTING_NI_DEVICE_ROUTES_EXTERN_H

#include "../ni_device_routes.h"

{externs}

#endif //_COMEDI_DRIVERS_NI_ROUTING_NI_DEVICE_ROUTES_EXTERN_H
"""

  single_output_file_top = """\
// SPDX-License-Identifier: GPL-2.0+
/*
 *  comedi/drivers/ni_routing/{filename}
 *  List of valid routes for specific NI boards.
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2016 Spencer E. Olson <olsonse@umich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 * The contents of this file are generated using the tools in
 * comedi/drivers/ni_routing/tools
 *
 * Please use those tools to help maintain the contents of this file.
 */

#include "../ni_device_routes.h"
#include "{extern_h}"

struct ni_device_routes {table_name} = {{\
"""

  def __init__(self, pattern='csv/device_routes/*.csv'):
    super(DeviceRoutes,self).__init__(pattern)

  def to_listinit(self):
    chunks = [ self.output_file_top,
      '',
      'struct ni_device_routes *const ni_device_routes_list[] = {'
    ]
    # put the sheets in lexical order of device numbers then bus
    sheets = sorted(self.items(), key=lambda i : tuple(i[0].split('-')[::-1]) )

    externs = []
    objs = [c_to_o(self.SET_C)]

    for sheet,D in sheets:
      S = sheet.lower()
      dev_table_name = 'ni_{}_device_routes'.format(S.replace('-','_'))
      sheet_filename = os.path.join(self.ITEMS_DIR,'{}.c'.format(S))
      externs.append('extern struct ni_device_routes {};'.format(dev_table_name))

      chunks.append('\t&{},'.format(dev_table_name))

      s_chunks = [
        self.single_output_file_top.format(
          filename    = sheet_filename,
          table_name  = dev_table_name,
          extern_h    = self.EXTERN_H,
        ),
        routedict_to_routelist_single(S, D),
        '};',
      ]

      objs.append(c_to_o(sheet_filename))

      with open(os.path.join(self.OUTPUT_DIR, sheet_filename), 'w') as f:
        f.write('\n'.join(s_chunks))
        f.write('\n')

    with open(os.path.join(self.OUTPUT_DIR, self.MKFILE_SEGMENTS), 'w') as f:
      f.write('# This is the segment that should be included in comedi/drivers/Makefile\n')
      f.write('ni_routing-objs\t\t\t\t+= \\\n')
      f.write('\n'.join(objs))
      f.write('\n')

    EXTERN_H = os.path.join(self.ITEMS_DIR, self.EXTERN_H)
    with open(os.path.join(self.OUTPUT_DIR, EXTERN_H), 'w') as f:
      f.write(self.extern_header.format(
        filename=EXTERN_H, externs='\n'.join(externs)))

    chunks.append('\tNULL,') # terminate list
    chunks.append('};')
    return '\n'.join(chunks)

  def save(self):
    filename=os.path.join(self.OUTPUT_DIR, self.SET_C)

    try:
      os.makedirs(os.path.join(self.OUTPUT_DIR, self.ITEMS_DIR))
    except:
      pass
    with open(filename,'w') as f:
      f.write( self.to_listinit() )
      f.write( '\n' )


class RouteValues(CSVCollection):
  MKFILE_SEGMENTS = 'route-values.mk'
  SET_C = 'ni_route_values.c'
  ITEMS_DIR = 'ni_route_values'
  EXTERN_H = 'all.h'
  OUTPUT_DIR = 'c'

  output_file_top = """\
// SPDX-License-Identifier: GPL-2.0+
/*
 *  comedi/drivers/ni_routing/{filename}
 *  Route information for NI boards.
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2016 Spencer E. Olson <olsonse@umich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 * This file includes the tables that are a list of all the values of various
 * signals routes available on NI hardware.  In many cases, one does not
 * explicitly make these routes, rather one might indicate that something is
 * used as the source of one particular trigger or another (using
 * *_src=TRIG_EXT).
 *
 * The contents of this file are generated using the tools in
 * comedi/drivers/ni_routing/tools
 *
 * Please use those tools to help maintain the contents of this file.
 */

#include "ni_route_values.h"
#include "{extern_h}"\
""".format(filename=SET_C, extern_h=os.path.join(ITEMS_DIR, EXTERN_H))

  extern_header = """\
/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  comedi/drivers/ni_routing/{filename}
 *  List of valid routes for specific NI boards.
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2016 Spencer E. Olson <olsonse@umich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 * The contents of this file are generated using the tools in
 * comedi/drivers/ni_routing/tools
 *
 * Please use those tools to help maintain the contents of this file.
 */

#ifndef _COMEDI_DRIVERS_NI_ROUTING_NI_ROUTE_VALUES_EXTERN_H
#define _COMEDI_DRIVERS_NI_ROUTING_NI_ROUTE_VALUES_EXTERN_H

#include "../ni_route_values.h"

{externs}

#endif //_COMEDI_DRIVERS_NI_ROUTING_NI_ROUTE_VALUES_EXTERN_H
"""

  single_output_file_top = """\
// SPDX-License-Identifier: GPL-2.0+
/*
 *  comedi/drivers/ni_routing/{filename}
 *  Route information for {sheet} boards.
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2016 Spencer E. Olson <olsonse@umich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 * This file includes a list of all the values of various signals routes
 * available on NI 660x hardware.  In many cases, one does not explicitly make
 * these routes, rather one might indicate that something is used as the source
 * of one particular trigger or another (using *_src=TRIG_EXT).
 *
 * The contents of this file can be generated using the tools in
 * comedi/drivers/ni_routing/tools.  This file also contains specific notes to
 * this family of devices.
 *
 * Please use those tools to help maintain the contents of this file, but be
 * mindful to not lose the notes already made in this file, since these notes
 * are critical to a complete undertsanding of the register values of this
 * family.
 */

#include "../ni_route_values.h"
#include "{extern_h}"

const struct family_route_values {table_name} = {{\
"""

  def __init__(self, pattern='csv/route_values/*.csv'):
    super(RouteValues,self).__init__(pattern)

  def to_structinit(self):
    chunks = [ self.output_file_top,
      '',
      'const struct family_route_values *const ni_all_route_values[] = {'
    ]
    # put the sheets in lexical order for consistency
    sheets = sorted(self.items(), key=lambda i : i[0] )

    externs = []
    objs = [c_to_o(self.SET_C)]

    for sheet,D in sheets:
      S = sheet.lower()
      fam_table_name = '{}_route_values'.format(S.replace('-','_'))
      sheet_filename = os.path.join(self.ITEMS_DIR,'{}.c'.format(S))
      externs.append('extern const struct family_route_values {};'.format(fam_table_name))

      chunks.append('\t&{},'.format(fam_table_name))

      s_chunks = [
        self.single_output_file_top.format(
          filename    = sheet_filename,
          sheet       = sheet.upper(),
          table_name  = fam_table_name,
          extern_h    = self.EXTERN_H,
        ),
        routedict_to_structinit_single(S, D),
        '};',
      ]

      objs.append(c_to_o(sheet_filename))

      with open(os.path.join(self.OUTPUT_DIR, sheet_filename), 'w') as f:
        f.write('\n'.join(s_chunks))
        f.write( '\n' )

    with open(os.path.join(self.OUTPUT_DIR, self.MKFILE_SEGMENTS), 'w') as f:
      f.write('# This is the segment that should be included in comedi/drivers/Makefile\n')
      f.write('ni_routing-objs\t\t\t\t+= \\\n')
      f.write('\n'.join(objs))
      f.write('\n')

    EXTERN_H = os.path.join(self.ITEMS_DIR, self.EXTERN_H)
    with open(os.path.join(self.OUTPUT_DIR, EXTERN_H), 'w') as f:
      f.write(self.extern_header.format(
        filename=EXTERN_H, externs='\n'.join(externs)))

    chunks.append('\tNULL,') # terminate list
    chunks.append('};')
    return '\n'.join(chunks)

  def save(self):
    filename=os.path.join(self.OUTPUT_DIR, self.SET_C)

    try:
      os.makedirs(os.path.join(self.OUTPUT_DIR, self.ITEMS_DIR))
    except:
      pass
    with open(filename,'w') as f:
      f.write( self.to_structinit() )
      f.write( '\n' )



if __name__ == '__main__':
  import argparse
  parser = argparse.ArgumentParser()
  parser.add_argument( '--route_values', action='store_true',
    help='Extract route values from csv/route_values/*.csv' )
  parser.add_argument( '--device_routes', action='store_true',
    help='Extract route values from csv/device_routes/*.csv' )
  args = parser.parse_args()
  KL = list()
  if args.route_values:
    KL.append( RouteValues )
  if args.device_routes:
    KL.append( DeviceRoutes )
  if not KL:
    parser.error('nothing to do...')
  for K in KL:
    doc = K()
    doc.save()
