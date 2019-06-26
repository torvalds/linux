#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
# vim: ts=2:sw=2:et:tw=80:nowrap

from os import path
import os, csv
from itertools import chain

from csv_collection import CSVCollection
from ni_names import value_to_name
import ni_values

CSV_DIR = 'csv'

def iter_src_values(D):
  return D.items()

def iter_src(D):
  for dest in D:
    yield dest, 1

def create_csv(name, D, src_iter):
  # have to change dest->{src:val} to src->{dest:val}
  fieldnames = [value_to_name[i] for i in sorted(D.keys())]
  fieldnames.insert(0, CSVCollection.source_column_name)

  S = dict()
  for dest, srcD in D.items():
    for src,val in src_iter(srcD):
      S.setdefault(src,{})[dest] = val

  S = sorted(S.items(), key = lambda src_destD : src_destD[0])


  csv_fname = path.join(CSV_DIR, name + '.csv')
  with open(csv_fname, 'w') as F_csv:
    dR = csv.DictWriter(F_csv, fieldnames, delimiter=';', quotechar='"')
    dR.writeheader()

    # now change the json back into the csv dictionaries
    rows = [
      dict(chain(
        ((CSVCollection.source_column_name,value_to_name[src]),),
        *(((value_to_name[dest],v),) for dest,v in destD.items())
      ))
      for src, destD in S
    ]

    dR.writerows(rows)


def to_csv():
  for d in ['route_values', 'device_routes']:
    try:
      os.makedirs(path.join(CSV_DIR,d))
    except:
      pass

  for family, dst_src_map in ni_values.ni_route_values.items():
    create_csv(path.join('route_values',family), dst_src_map, iter_src_values)

  for device, dst_src_map in ni_values.ni_device_routes.items():
    create_csv(path.join('device_routes',device), dst_src_map, iter_src)


if __name__ == '__main__':
  to_csv()
