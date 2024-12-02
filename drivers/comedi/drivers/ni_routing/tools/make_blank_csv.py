#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+

from os import path
import os, csv

from csv_collection import CSVCollection
from ni_names import value_to_name

CSV_DIR = 'csv'

def to_csv():
  try:
    os.makedirs(CSV_DIR)
  except:
    pass

  csv_fname = path.join(CSV_DIR, 'blank_route_table.csv')

  fieldnames = [sig for sig_val, sig in sorted(value_to_name.items())]
  fieldnames.insert(0, CSVCollection.source_column_name)

  with open(csv_fname, 'w') as F_csv:
    dR = csv.DictWriter(F_csv, fieldnames, delimiter=';', quotechar='"')
    dR.writeheader()

    for sig in fieldnames[1:]:
      dR.writerow({CSVCollection.source_column_name: sig})

if __name__ == '__main__':
  to_csv()
